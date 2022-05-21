#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <unistd.h>
#include <limits.h>
#include <dirent.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#define MAIN_ERROR_ARGC "Error in MAIN: Invalid number of arguments\n"
#define MAIN_ERROR_OPENDIR "Error in MAIN: Error in opendir()\n"
#define MAIN_ERROR_ARGV "Error in MAIN: Directory given can't be searched\n"
#define MAIN_ERROR_MALLOC "Error in MAIN: Failed malloc\n"
#define MAIN_ERROR_THREAD "Error in MAIN: Failed creating thread\n"
#define MAIN_ERROR_MTX "Error in MAIN: Failed Initializing a mutex\n"
#define MAIN_ERROR_CND "Error in MAIN: Failed Initializing a contidtion\n"
#define MAIN_ERROR_JOIN "Error in MAIN: Failed joining threads\n"
#define THREAD_ERROR_MALLOC "Error in THREAD: Failed malloc\n"
#define THREAD_ERROR_OPENDIR "Error in THREAD: Failed opendir()\n"

/* an element in queue- in our case it's a directory*/
typedef struct Element
{
    char *dir_name; /* directory's name */
    struct Element *next;    /* pointer to next element in queue (next directory) */

} DIR_element;

typedef struct Queue
{
    int size;
    struct Element *first; /* pointer to first element in queue (first directory) */
    struct Element *last;  /* pointer to last element in queue (last directory) */

} DIR_queue;

DIR_queue *dir_queue;
mtx_t main_mutex; // Lock mutex if a thread handling queue
mtx_t allThreadsMutex; // Lock mutex until all threads created
mtx_t update_match; // Lock mutex if a thread handling queue

/* We have 3 condition that need to be checked*/
cnd_t allThreadsCreated; // The main thread use it to signal that the searching should start (all searching threads has been created).
cnd_t threadCreated;     // Use it to signal that a thread was created
cnd_t isQueueEmpty;      // if the queue is empty or added a directory
cnd_t updateWaiting;      // if the queue is empty or added a directory
cnd_t updateKWaiting;      // if the queue is empty or added a directory

int numOfThreads; // number of thread that should be created- given in argv[3]
int numOfRunningThreads=0;// updated when any thread that is created or destroyed
int numOfWaitingThreads=0; // number of waiting threads- waiting for a non-empty queue or for other threrads (or waiting to be exited)
char *term; // the value in argv[2]
int errorThread = 0; // boolean parameter- if an error occured in one of the thread- set 1
int numMatchedFiles=0; // number of files that were found under the given terms
int numOfthreadDoneWaiting=0;
int counterWaiting=0;
int isQempty=0;
int afterKwaiting=0;
int anyOnhold=0;
int numberOfThreadsHolding=0;

/*-------------------Declarations-------------------*/

/* Creates all mutexs and conditions*/
void createMutexCnd();

/* Removes first element from queue, copy it's directory name to a given address. returns 1 if an error occured,0- else*/
char* removeElemFromQ();

/* Inserts new element to the end of the queue. return 1 if error occured (can only be failed malloc) 0 if succeed*/
int insertElemToQ(char* dir_name);

/* Handling error in thread- print and exit thread*/
void KillThread();

/* Actions of a searching thread*/
int thread_func();

/* the main action of removing element*/
char* quickRemove();

/* Make all threads wait for wach other to be created- after it all threads can run*/
void waitForAll();

/* Check if number of K waiting threads and number of threads 
in hold equal to number of running thread (For thread in hold mode)*/ 
void checkHoldingExit();

/* Check if number of K waiting threads and number of threads 
in hold equal to number of running thread (For thread in waiting mode- one of K waiting)*/ 
void checkKWaitingExit();


int main(int argc, char *argv[])
{
    int return_value;
    int i;
    thrd_t *thread_ids;
    DIR_element *head;
    DIR *open_dir;
    
    /************** CHECK MAIN THREAD **************/
    /*Validate that the correct number of command line arguments is passed*/
    if (argc != 4)
    {
        fprintf(stderr, MAIN_ERROR_ARGC);
        exit(1);
    }
    // Initialize values
    term =argv[2];
    numOfThreads = atoi(argv[3]);
    
    void createMutexCnd();

    char *head_dir=malloc(PATH_MAX*sizeof(char));
    if(head_dir==NULL)
    {
        fprintf(stderr, MAIN_ERROR_MALLOC);
        exit(1);
    }
    // check if its return ERROR on NULL in argv[1]
    strcpy(head_dir,argv[1]);

    /* Check if the directory specified in argv[1] can be searched */
    open_dir = opendir(head_dir);
    if(open_dir==NULL)
    { 
        if(errno!=EACCES)
        {// in case of an error in opendir
            fprintf(stderr,MAIN_ERROR_OPENDIR);
            exit(1);
        }
        else
        {// can't happend here
            printf("Directory %s: Permission denied.\n", head_dir);
            exit(1);
        }
    }

    /* Create a FIFO queue that holds directories */
    dir_queue = (DIR_queue*)malloc(sizeof(DIR_queue));
    if (dir_queue == NULL)
    {
        fprintf(stderr, MAIN_ERROR_MALLOC);
        exit(1);
    }

    /* Put the search root directory in the queue*/
    head=(DIR_element*)malloc(sizeof(DIR_element));
    if (head == NULL)
    {
        fprintf(stderr, MAIN_ERROR_MALLOC);
        exit(1);
    }
    head->dir_name=head_dir;
    head->next=NULL;
    
    dir_queue->first=head;
    dir_queue->last=head;
    dir_queue->size=1;

    /* Create n(=argv[3]) searching threads */
    thread_ids =malloc(numOfThreads * sizeof(thrd_t));
    if (thread_ids == NULL)
    {
        fprintf(stderr, MAIN_ERROR_MALLOC);
        exit(1);
    }
    for (i = 0; i < numOfThreads; i++)
    {
        return_value = thrd_create(&thread_ids[i], thread_func,NULL);
        if (return_value != thrd_success)
        {
            fprintf(stderr, MAIN_ERROR_THREAD);
            exit(1);
        }
    }

    /*Wait for all other searching threads to be created and for the main thread to signal that the searching should start. */
    mtx_lock(&main_mutex);
    while (numOfRunningThreads < numOfThreads)
    {
        /* waiting for threadCreated signal*/
        cnd_wait(&threadCreated, &main_mutex);
    }

    mtx_unlock(&main_mutex);

    // signal all threads that all threads were created
    cnd_broadcast(&allThreadsCreated);

    // --- Wait for threads to finish ------------------
    for (long t = 0; t < numOfThreads; ++t)
    {
        return_value = thrd_join(thread_ids[t], NULL);
        if (return_value != thrd_success)
        {
            fprintf(stderr, MAIN_ERROR_JOIN);
            exit(1);
        }
    }
    closedir(open_dir);

    mtx_destroy(&main_mutex);

    printf("Done searching, found %d files\n", numMatchedFiles);
    if (!errorThread)
    {
        // no thread has encouterd an error
        exit(0);
    }
    exit(1);
}

void waitForAll()
{
    // waits untill all threads are created. update main that a thread was creating by update num of created threads and send a signal
    mtx_lock(&main_mutex);
    // this thread in now running
    numOfRunningThreads++;
    // this thren has been created- update main
    cnd_signal(&threadCreated);
    // wait for all thread to be created
    cnd_wait(&allThreadsCreated, &main_mutex);
    mtx_unlock(&main_mutex);
}

void checkHoldingExit()
{
    if(dir_queue->first==NULL)
    {
        if(numOfWaitingThreads+numberOfThreadsHolding==numOfRunningThreads)
        {
            numOfRunningThreads--;
            // current thread is a in "numberOfThreadsHolding" count
            numberOfThreadsHolding--;
            mtx_unlock(&main_mutex);
            cnd_broadcast(&updateWaiting);
            cnd_broadcast(&isQueueEmpty);
            thrd_exit(1);
        }
    }
    else
    {
        cnd_broadcast(&updateWaiting);
        cnd_broadcast(&isQueueEmpty);
    }
    
}

void checkKWaitingExit()
{
    if(dir_queue->first==NULL)
    {
        if(numOfWaitingThreads+numberOfThreadsHolding==numOfRunningThreads)
        {
            numOfRunningThreads--;
            // current thread is a in "numOfWaitingThreads" count
            numOfWaitingThreads--;
            mtx_unlock(&main_mutex);
            cnd_broadcast(&updateWaiting);
            cnd_broadcast(&isQueueEmpty);
            thrd_exit(1);
        }
    }
    else
    {
        cnd_broadcast(&updateWaiting);
        cnd_broadcast(&isQueueEmpty);
    }
}


int thread_func()
{
    int return_value;
    DIR *head_open; // for open head of queue directory
    char *entry_path; //saves path of a directory that will be added to queue
    DIR *searchable_dir; // check if current entry is a searchable directory
    char *check_term;
    struct dirent *entry;
    struct stat filestat;

    int qWasEmpty=0; // if thread got in when queue was empty
    int numThreadToWait; // if thread got in when queue was empty- it needs to wake after thread number "numThreadToWait" is done
    int isKwaiting; // if thread is one of the K waiting thread for queue to become non-empty
    int BoolWaitedOutside=0; // boolean for if thread is not one of K queue and needs to wait for them to complete

    waitForAll();

    while(1)
    {
        /* set default variables 
        qWasEmpty-if thread got in and waited because queue was empty,
        isKwaiting- if thread got in when there is a thread that waits- it's part of K waiting threads,
        numThreadToWait- if thread got in when there is a thread that waits
        for queue to be non empty- it needs to wake up when numThreadToWait=numOfthreadDoneWaiting,
        BoolWaitedOutside- if thread got in while K threads are in run.
        */
        qWasEmpty=0; 
        numThreadToWait=0;
        isKwaiting=0;
        BoolWaitedOutside=0;

        mtx_lock(&main_mutex);

        /* first K waiting threads are ON right now- so current thread need to wait
        isQempty=1 from first thread got in and waited because queue was empty till the
        last thread before queue got non-empty enterd and made his remove.
        So while K threads are in progress- every other thread needs to wait*/ 
        while (isQempty)
        {
            // update numberOfThreadsHolding (only once for each diffrent thread)
            if(BoolWaitedOutside==0)
            {
                BoolWaitedOutside=1;
                numberOfThreadsHolding++;
            }

            // current thread entered but all other threads are waiting for K threads to finish and they are waiting for queue to be non-empty.
            checkHoldingExit();
            // wait till K threads done their work
            cnd_wait(&updateWaiting,&main_mutex);
            checkHoldingExit();
        }
        // if thread waited for K threads to be done- need to update numberOfThreadsHolding
        if(BoolWaitedOutside)
        {
            numberOfThreadsHolding--;
        }
        // take care of K waiting- thread got in to if there is a thread waiting for queue to become non-empty
        if(numOfWaitingThreads>0)
        {
            // one of kWaiting
            isKwaiting=1;
            numOfWaitingThreads++; // current thread is also waiting
            numThreadToWait=counterWaiting;
            counterWaiting++;

            checkKWaitingExit();
            
            // wake up thread at it's time
            while(numThreadToWait<numOfthreadDoneWaiting)
            {
                checkKWaitingExit();
                cnd_wait(&updateWaiting,&main_mutex);
                checkKWaitingExit();
            }
        }
        // no thread (beside threads in holding) is waiting at the moment so numOfWaitingThreads==0
        else
        {
            // if thread got in when queue was empty
            if(dir_queue->first==NULL)
            {
                qWasEmpty=1;// thread waiting because queue is empty
                anyOnhold=1;// first thread out of K in waiting so there is at least one thread on hold
                numOfWaitingThreads++;
                numThreadToWait=counterWaiting;
                counterWaiting++;
            }
        }        
        char* head_path=removeElemFromQ();
        // if thread was one of the K waiting
        if(qWasEmpty || isKwaiting)
        { // update number of waiting threads
            numOfWaitingThreads--;
            numOfthreadDoneWaiting++;

            // Done ALL k threads- cuurent thread is last k thread
            if(numOfWaitingThreads==0)
            { // last thread done waiting
                counterWaiting=0;
                isQempty=0;
                anyOnhold=0;
                numOfthreadDoneWaiting=0;
            }
        }
        
        // wake up K waiting
        cnd_broadcast(&updateWaiting);

        mtx_unlock(&main_mutex);

        head_open = opendir(head_path);// should be able to read
        if(head_open==NULL)
        { 
            if(errno!=EACCES)
            {// in case of an error in opendir
                errorThread=1;
                fprintf(stderr,THREAD_ERROR_OPENDIR);
                KillThread();
            }
        }
        /* head_path is a searchable directory*/
        /* https://stackoverflow.com/questions/60535786/stat-using-s-isdir-dont-seem-to-always-work */
        /* iterate through each directory entry (dirent) in the directory obtained from the queue*/
        while ((entry = readdir(head_open)) != NULL)
        {
            entry_path = (char*)malloc(PATH_MAX * sizeof(char));
            if (entry_path == NULL)
            {
                errorThread=1;
                fprintf(stderr,THREAD_ERROR_MALLOC);
                KillThread();
            }

            //build full path
            strcpy(entry_path, head_path);
            strcat(entry_path, "/");
            strcat(entry_path, entry->d_name);

            if(stat(entry_path,&filestat)==-1)
            {
                errorThread=1;
                fprintf(stderr,THREAD_ERROR_MALLOC);
                KillThread();
            }

            if(S_ISDIR(filestat.st_mode))
            {// entry is a DIR
                if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 )
                {
                    //free(entry_path);
                    continue;
                }
                else
                {
                    // check if dir is searchable
                    searchable_dir = opendir(entry_path);
                    if(searchable_dir==NULL)
                    { 
                        if(errno!=EACCES)
                        {// in case of an error in opendir
                            errorThread=1;
                            fprintf(stderr,THREAD_ERROR_OPENDIR);
                            KillThread();
                        }
                        else
                        {// can't happend here
                            printf("Directory %s: Permission denied.\n", entry_path);
                            continue;
                        }
                    }
                    else
                    {
                        // insert new directory path
                        return_value = insertElemToQ(entry_path);
                        if (return_value==1)
                        {
                            errorThread=1;
                            fprintf(stderr,THREAD_ERROR_MALLOC);
                            KillThread();
                        }
                    }
                    closedir(searchable_dir);
                }
            } 
            else 
            {// entry is a file- check the term
                check_term = strstr(entry->d_name, term);
                if (check_term != NULL)
                {
                    printf("%s\n",entry_path);
                    mtx_lock(&update_match);
                    numMatchedFiles++;
                    mtx_unlock(&update_match);
                }
            }
        }  
        closedir(head_open);
    }
}


char* removeElemFromQ()
{
    while(dir_queue->first == NULL)
    {
        checkKWaitingExit();
        cnd_wait(&isQueueEmpty,&main_mutex);
        checkKWaitingExit();
    }
    return quickRemove();
}


char* quickRemove()
{
    DIR_element *rem_first;
    // dir_queue-> size!=0
    rem_first = dir_queue->first;
    char* dir_name= rem_first->dir_name;
    if(rem_first->next!=NULL)
    {
        dir_queue->first = rem_first->next;
    }
    else
    {
        dir_queue->first = NULL;
        dir_queue->last = NULL;
    }
    dir_queue->size = dir_queue->size - 1;
    free(rem_first);
    return dir_name;
}

void createMutexCnd()
{
    int return_value;
    /* Initialize mutex and condition variable objects */
    return_value = mtx_init(&main_mutex,mtx_plain); // MIGHT NEED TO CHANGE mtx_plain
    if (return_value != thrd_success)
    {
        fprintf(stderr, MAIN_ERROR_MTX);
        exit(1);
    }
    return_value = mtx_init(&allThreadsMutex,mtx_plain); // MIGHT NEED TO CHANGE mtx_plain
    if (return_value != thrd_success)
    {
        fprintf(stderr, MAIN_ERROR_MTX);
        exit(1);
    }
    return_value = mtx_init(&update_match,mtx_plain); // MIGHT NEED TO CHANGE mtx_plain
    if (return_value != thrd_success)
    {
        fprintf(stderr, MAIN_ERROR_MTX);
        exit(1);
    }
    return_value = cnd_init(&allThreadsCreated);
    if (return_value != thrd_success)
    {
        fprintf(stderr, MAIN_ERROR_CND);
        exit(1);
    }
    return_value = cnd_init(&threadCreated);
    if (return_value != thrd_success)
    {
        fprintf(stderr, MAIN_ERROR_CND);
        exit(1);
    }
    return_value = cnd_init(&isQueueEmpty);
    if (return_value != thrd_success)
    {
        fprintf(stderr, MAIN_ERROR_CND);
        exit(1);
    }
    return_value = cnd_init(&updateWaiting);
    if (return_value != thrd_success)
    {
        fprintf(stderr, MAIN_ERROR_CND);
        exit(1);
    }
    return_value = cnd_init(&updateKWaiting);
    if (return_value != thrd_success)
    {
        fprintf(stderr, MAIN_ERROR_CND);
        exit(1);
    }
}
    
int insertElemToQ(char* dir_name)
{
    DIR_element *dir_obj;
    dir_obj = (DIR_element*)malloc(sizeof(DIR_element));
    // malloc failed
    if (dir_obj == NULL)
    {
        return 1;
    }
    dir_obj->dir_name=dir_name;
    dir_obj->next=NULL;
    mtx_lock(&main_mutex);
    // check if can be replaced with one of them!!!!!!!!!!!!!!!
    /* if queue is empty- need to set up first */
    if (dir_queue->first == NULL)
    {
        dir_queue->first = dir_obj;
        dir_queue->last = dir_obj;
        dir_queue->size = 1;
    }
    else
    { // queue isn't empty
        if(dir_queue->size==1)
        {
            dir_queue->first->next=dir_obj;
            dir_queue->last=dir_obj;
        }
        else
        {
            dir_queue->last->next = dir_obj;
            dir_queue->last = dir_obj;
        }
        dir_queue->last=dir_obj;
        dir_queue->size += 1;
    }

    // K threads are waiting and queue became nonempty- so update isQempty=1
    // stays isQempty=1 until thread number K is done
    if(dir_queue->size == 1 && anyOnhold==1)
    {
        isQempty=1;
    }
    mtx_unlock(&main_mutex);
    cnd_broadcast(&isQueueEmpty);
    return 0;
}

void KillThread()
{
    mtx_lock(&main_mutex);
    numOfRunningThreads--;
    mtx_unlock(&main_mutex);
    cnd_broadcast(&isQueueEmpty);
    cnd_broadcast(&updateWaiting);
    thrd_exit(1);
}
