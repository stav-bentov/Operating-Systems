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
#include <stdatomic.h>

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

#define ERROR_INT -1
#define PERMISSION_INT 1
#define SEARCHBLE_INT 0
#define ERROR 1
#define SUCCESES 0

/*-------------------Declarations-------------------*/

/* Creates all mutexs and conditions*/
void createMutexCnd();

/* Removes first element from queue, copy it's directory name to a given address. returns 1 if an error occured,0- else*/
char* dequeueDir();

/* Inserts new element to the end of the queue. return 1 if error occured (can only be failed malloc) 0 if succeed*/
int enqueueDir(char* dir_path);

/* Handling error in thread- print and exit thread*/
void KillThread();

/* Actions of a searching thread*/
int thread_func();

/* Searching in a given directory for mathcing files or new directories to add to dir_queue*/ 
void thread_search(char *head_path);

/* The main action of removing element*/
char* quickRemove();

/* Make all threads wait for wach other to be created- after it all threads can run*/
void waitForAll();

/* Check if number of K waiting threads and number of threads 
in hold equal to number of running thread (For thread in hold mode)*/ 
void checkHoldingExit();

/* Check if number of K waiting threads and number of threads 
in hold equal to number of running thread (For thread in waiting mode- one of K waiting)*/ 
void checkKWaitingExit();

/* given a directory's path- check if it is searchable, in case of an error- return ERROR_INT*/
int isSearchble(char* dir);

// An element in queue- in our case it's a directory.
typedef struct Element
{
    char *dir_path; // Directory's name.
    struct Element *next;// Pointer to next element in queue (next directory).

} DIR_element;

typedef struct Queue
{
    int size;
    struct Element *first; // Pointer to first element in queue (first directory).
    struct Element *last;  // Pointer to last element in queue (last directory).

} DIR_queue;

DIR_queue *dir_queue;

// Lock mutex if a thread handling queue.
mtx_t main_mutex; 
// Lock mutex until all threads created.
mtx_t allThreadsMutex; 

// The main thread use it to signal that the searching should start (all searching threads has been created).
cnd_t allThreadsCreated; 
// Use it to signal that a thread was created.
cnd_t threadCreated;    
// If the queue is empty or added a directory. 
cnd_t isQueueEmpty;     
// If a waiting thread done it's job 
cnd_t updateWaiting;      

// Number of thread that should be created- given in argv[3].
int numOfThreads; 
// Updated when any thread that is created or destroyed.
int numOfRunningThreads=0;
// Number of waiting threads- waiting for a non-empty queue or for other threrads (or waiting to be exited).
int numOfWaitingThreads=0; 
// The value in argv[2]
char *term; 

// Boolean parameter- if an error occured in one of the thread- set 1.
atomic_int errorThread = 0; 

// Number of files that were found under the given terms.
atomic_int numMatchedFiles=0; 

/* Counter for the number of threads done thier part after waked up- 
 make the right thread wake up at his time (right after the thread that entered before him).*/
int numOfthreadDoneWaiting=0;

// Counter for numbre of threads waiting for queue to become non-empty.
int counterWaiting=0;

// Boolean parameter- When queue becomes non-empty after it was- the threads that waited for queue to become non-empty.
int isWaitingNowRunning=0;

// When first thread is waiting for queue to become non-empty it's value will be 1, when all waiting thread done their run -0 again.
int anyOnhold=0;

// Number of threads waiting for K threads (that were sleeping because of empty queue) to finish their run.
int numberOfThreadsHolding=0;

/*-------------------Functions-------------------*/
int main(int argc, char *argv[])
{
    int return_value;
    int i;
    thrd_t *thread_ids;
    
    /*Validate that the correct number of command line arguments is passed*/
    if (argc != 4)
    {
        fprintf(stderr, MAIN_ERROR_ARGC);
        exit(ERROR);
    }
    // Initialize values
    term =argv[2];
    numOfThreads = atoi(argv[3]);
    
    void createMutexCnd();

    char *head_dir=malloc(PATH_MAX*sizeof(char));
    if(head_dir==NULL)
    {
        fprintf(stderr, MAIN_ERROR_MALLOC);
        exit(ERROR);
    }
    // Check if its return ERROR on NULL in argv[1]
    strcpy(head_dir,argv[1]);

    /* Check if the directory specified in argv[1] can be searched */
    return_value=isSearchble(head_dir);
    if(return_value!=SEARCHBLE_INT)
    { // Error or permission denied
        if(return_value==ERROR_INT)
        {
            fprintf(stderr, MAIN_ERROR_OPENDIR);
            exit(ERROR);
        }
        else
        {
            fprintf(stderr, MAIN_ERROR_ARGV);
            exit(ERROR);
        }
    }

    /* Create a FIFO queue that holds directories */
    dir_queue = (DIR_queue*)malloc(sizeof(DIR_queue));
    if (dir_queue == NULL)
    {
        fprintf(stderr, MAIN_ERROR_MALLOC);
        exit(ERROR);
    }
    dir_queue->first=NULL;
    dir_queue->last=NULL;
    dir_queue->size=0;

    /* Put the search root directory in the queue*/
    return_value=enqueueDir(head_dir);
    if (return_value==ERROR)
    {
        fprintf(stderr, MAIN_ERROR_MALLOC);
        exit(ERROR);
    }

    /* Create n(=argv[3]) searching threads */
    thread_ids =malloc(numOfThreads * sizeof(thrd_t));
    if (thread_ids == NULL)
    {
        fprintf(stderr, MAIN_ERROR_MALLOC);
        exit(ERROR);
    }
    for (i = 0; i < numOfThreads; i++)
    {
        return_value = thrd_create(&thread_ids[i], thread_func,NULL);
        if (return_value != thrd_success)
        {
            fprintf(stderr, MAIN_ERROR_THREAD);
            exit(ERROR);
        }
    }

    /*Wait for all other searching threads to be created and for the main thread to signal that the searching should start. */
    mtx_lock(&main_mutex);
    while (numOfRunningThreads < numOfThreads)
    {
        /* Waiting for threadCreated signal*/
        cnd_wait(&threadCreated, &main_mutex);
    }

    mtx_unlock(&main_mutex);

    // Signal all threads that all threads were created
    cnd_broadcast(&allThreadsCreated);

    // ------------------ Wait for threads to finish ------------------
    for (long t = 0; t < numOfThreads; ++t)
    {
        return_value = thrd_join(thread_ids[t], NULL);
        if (return_value != thrd_success)
        {
            fprintf(stderr, MAIN_ERROR_JOIN);
            exit(ERROR);
        }
    }

    mtx_destroy(&main_mutex);
    printf("Done searching, found %d files\n", numMatchedFiles);
    exit(errorThread);
}


int thread_func()
{
    int qWasEmpty=0; // if thread got in when queue was empty
    int numThreadToWait; // if thread got in when queue was empty- it needs to wake after thread number "numThreadToWait" is done
    int isKwaiting; // if thread is one of the K waiting thread for queue to become non-empty
    int BoolWaitedOutside=0; // boolean for if thread is not one of K queue and needs to wait for them to complete

    waitForAll();

    while(1)
    {
        /* Set default variables 
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
        isWaitingNowRunning=1 from first thread got in and waited because queue was empty till the
        last thread before queue got non-empty enterd and made his remove.
        So while K threads are in progress- every other thread needs to wait*/ 
        while (isWaitingNowRunning)
        {
            // Update numberOfThreadsHolding (only once for each diffrent thread)
            if(BoolWaitedOutside==0)
            {
                BoolWaitedOutside=1;
                numberOfThreadsHolding++;
            }

            // Current thread entered but all other threads are waiting for K threads to finish and they are waiting for queue to be non-empty.
            checkHoldingExit();
            // Wait till K threads done their work
            cnd_wait(&updateWaiting,&main_mutex);
            checkHoldingExit();
        }
        // If thread waited for K threads to be done- need to update numberOfThreadsHolding
        if(BoolWaitedOutside)
        {
            numberOfThreadsHolding--;
        }
        // Take care of K waiting- thread got in to if there is a thread waiting for queue to become non-empty
        if(numOfWaitingThreads>0)
        {
            // Current thread is Waiting thread due to queue is empty
            isKwaiting=1;
            numOfWaitingThreads++; // Current thread is also waiting
            numThreadToWait=counterWaiting; // Current thread needs to wake up when numThreadToWait=numOfthreadDoneWaiting
            counterWaiting++;

            checkKWaitingExit();
            
            // Wake up thread at it's time
            while(numThreadToWait<numOfthreadDoneWaiting)
            {
                checkKWaitingExit();
                cnd_wait(&updateWaiting,&main_mutex);
                checkKWaitingExit();
            }
        }
        // No thread (beside threads in holding) is waiting at the moment so numOfWaitingThreads==0
        else
        {
            // If thread got in when queue was empty
            if(dir_queue->first==NULL)
            {
                qWasEmpty=1;// Thread waiting because queue is empty
                anyOnhold=1;// First thread out of K in waiting so there is at least one thread on hold
                numOfWaitingThreads++;
                numThreadToWait=counterWaiting;
                counterWaiting++;
            }
        }
        char* head_path=dequeueDir();

        // If thread was one of the K waiting
        if(qWasEmpty || isKwaiting)
        { // Update number of waiting threads
            numOfWaitingThreads--;
            numOfthreadDoneWaiting++;

            // Done ALL k threads- cuurent thread is last k thread
            if(numOfWaitingThreads==0)
            { // Last thread done waiting
                counterWaiting=0;
                isWaitingNowRunning=0;
                anyOnhold=0;
                numOfthreadDoneWaiting=0;
            }
        }
        
        // Wake up K waiting
        cnd_broadcast(&updateWaiting);
        mtx_unlock(&main_mutex);

        thread_search(head_path);
    }
}

void thread_search(char *head_path)
{
    int return_value;
    DIR *head_open; // for open head of queue directory
    char *entry_path; //saves path of a directory that will be added to queue
    struct dirent *entry;
    char *check_term;
    struct stat filestat;

    head_open = opendir(head_path);// Should be able to read
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

        //Build full path
        strcpy(entry_path, head_path);
        strcat(entry_path, "/");
        strcat(entry_path, entry->d_name);

        if(stat(entry_path,&filestat)==-1)
        {
            errorThread=1;
            fprintf(stderr,THREAD_ERROR_MALLOC);
            KillThread();
        }

        // Check entry's type
        if(S_ISDIR(filestat.st_mode))
        {// entry is a DIR
            if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 )
            {
                continue;
            }
            else
            {
                // Check if dir is searchable
                return_value=isSearchble(entry_path);
                if(return_value!=SEARCHBLE_INT)
                { // Error or permission denied
                    if(return_value==ERROR_INT)
                    {// In case of an error in opendir
                        errorThread=1;
                        fprintf(stderr,THREAD_ERROR_OPENDIR);
                        KillThread();
                    }
                    else
                    {
                        printf("Directory %s: Permission denied.\n", entry_path);
                        continue;
                    }
                }
                // Insert new directory path
                return_value = enqueueDir(entry_path);
                if (return_value==ERROR)
                {
                    errorThread=1;
                    fprintf(stderr,THREAD_ERROR_MALLOC);
                    KillThread();
                }
            }
        } 
        else 
        {// Entry is a file- check the term
            check_term = strstr(entry->d_name, term);
            if (check_term != NULL)
            {
                printf("%s\n",entry_path);
                numMatchedFiles++;
            }
        }
    }  
    closedir(head_open);
}

int isSearchble(char* dir)
{
    DIR *open_dir;
    open_dir = opendir(dir);
    if(open_dir==NULL)
    { 
        if(errno!=EACCES)
        {// In case of an error in opendir
            return ERROR_INT;
        }
        else
        {// Permission denied
            return PERMISSION_INT;
        }
    }
    closedir(open_dir);
    return SEARCHBLE_INT;
}

void waitForAll()
{
    // Waits untill all threads are created. update main that a thread was creating by update num of created threads and send a signal
    mtx_lock(&main_mutex);
    // This thread in now running
    numOfRunningThreads++;
    // This thread has been created- update main
    cnd_signal(&threadCreated);
    // Wait for all thread to be created
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
            // Current thread is in "numberOfThreadsHolding" count
            numberOfThreadsHolding--;
            mtx_unlock(&main_mutex);
            cnd_broadcast(&updateWaiting);
            cnd_broadcast(&isQueueEmpty);
            thrd_exit(SUCCESES);
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
            thrd_exit(SUCCESES);
        }
    }
    else
    {
        cnd_broadcast(&updateWaiting);
        cnd_broadcast(&isQueueEmpty);
    }
}

char* dequeueDir()
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
    char* dir_path= rem_first->dir_path;
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
    return dir_path;
}

void createMutexCnd()
{
    int return_value;
    /* Initialize mutex and condition variable objects */
    return_value = mtx_init(&main_mutex,mtx_plain);
    if (return_value != thrd_success)
    {
        fprintf(stderr, MAIN_ERROR_MTX);
        exit(ERROR);
    }
    return_value = mtx_init(&allThreadsMutex,mtx_plain);
    if (return_value != thrd_success)
    {
        fprintf(stderr, MAIN_ERROR_MTX);
        exit(ERROR);
    }

    return_value = cnd_init(&allThreadsCreated);
    if (return_value != thrd_success)
    {
        fprintf(stderr, MAIN_ERROR_CND);
        exit(ERROR);
    }
    return_value = cnd_init(&threadCreated);
    if (return_value != thrd_success)
    {
        fprintf(stderr, MAIN_ERROR_CND);
        exit(ERROR);
    }
    return_value = cnd_init(&isQueueEmpty);
    if (return_value != thrd_success)
    {
        fprintf(stderr, MAIN_ERROR_CND);
        exit(ERROR);
    }
    return_value = cnd_init(&updateWaiting);
    if (return_value != thrd_success)
    {
        fprintf(stderr, MAIN_ERROR_CND);
        exit(ERROR);
    }
}
    
int enqueueDir(char* dir_path)
{
    DIR_element *dir_obj;
    dir_obj = (DIR_element*)malloc(sizeof(DIR_element));
    // Malloc failed
    if (dir_obj == NULL)
    {
        return ERROR;
    }
    dir_obj->dir_path=dir_path;
    dir_obj->next=NULL;
    mtx_lock(&main_mutex);
    
    /* If queue is empty- need to set up first */
    if (dir_queue->first == NULL)
    {
        dir_queue->first = dir_obj;
        dir_queue->last = dir_obj;
        dir_queue->size = 1;
    }
    else
    { // Queue isn't empty
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

    // K threads are waiting and queue became nonempty- so update isWaitingNowRunning=1
    // stays isWaitingNowRunning=1 until thread number K is done
    if(dir_queue->size == 1 && anyOnhold==1)
    {
        isWaitingNowRunning=1;
    }
    mtx_unlock(&main_mutex);
    cnd_broadcast(&isQueueEmpty);
    return SUCCESES;
}

void KillThread()
{
    mtx_lock(&main_mutex);
    numOfRunningThreads--;
    mtx_unlock(&main_mutex);
    cnd_broadcast(&isQueueEmpty);
    cnd_broadcast(&updateWaiting);
    thrd_exit(ERROR);
}
