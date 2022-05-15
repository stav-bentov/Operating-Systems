#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <unistd.h>
#include <limits.h>
#include <dirent.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#define MAIN_ERROR_ARGC "Error in MAIN: Invalid number of arguments\n"
#define MAIN_ERROR_ARGV "Error in MAIN: Directory given can't be searched\n"
#define MAIN_ERROR_MALLOC "Error in MAIN: Failed malloc\n"
#define MAIN_ERROR_THREAD "Error in MAIN: Failed creating thread\n"
#define MAIN_ERROR_MTX "Error in MAIN: Failed Initializing a mutex\n"
#define MAIN_ERROR_CND "Error in MAIN: Failed Initializing a contidtion\n"
#define MAIN_ERROR_JOIN "Error in MAIN: Failed joining threads\n"
#define THREAD_ERROR_MALLOC "Error in THREAD: Failed malloc\n"

#define THREAD_ERROR_REMOVE "Error: NULL pointer to the head of queue\n"
#define THREAD_ERROR_SEARCHABLE "Error: NULL pointer to the head of queue\n"

#define EXIT_FAILURE 1
#define EXIT_THREAD_NO_ERROR 0

/* an element in queue- in our case it's a directory*/
typedef struct Element
{
    char dir_name[PATH_MAX]; /* directory's name */
    struct Element *next;    /* pointer to next element in queue (next directory) */

} DIR_element;

typedef struct Queue
{
    int size;
    struct Element *first; /* pointer to first element in queue (first directory) */
    struct Element *last;  /* pointer to last element in queue (last directory) */

} DIR_queue;

DIR_queue *dir_queue;
mtx_t mutex; // Lock mutex if a thread handling queue
mtx_t allThreadsMutex; // Lock mutex until all threads created

/* We have 3 condition that need to be checked*/
cnd_t allThreadsCreated; // The main thread use it to signal that the searching should start (all searching threads has been created).
cnd_t threadCreated;     // Use it to signal that a thread was created
cnd_t isQueueEmpty;      // if the queue is empty or added a directory

int numOfRunningThreads=0;// updated when any thread that is created or destroyed
int numOfThreads; // number of thread that should be created- given in argv[3]
int numOfThreadsWaiting = 0; // number of thread that are sleeping/waiting
int numOfThreadsKilled = 0;
int errorThread = 0; // boolean parameter- if an error occured in one of the thread- set 1
int numMatchedFiles=0; // number of files that were found under the given terms
char *term; // the value in argv[2]

/*-------------------Declarations-------------------*/
/* remove first element from queue, copy it's directory name to a given address. returns 1 if an error occured,0- else*/
int removeElemFromQ(char *dir_name);
/* insert new element to the end of the queue. return 1 if error occured (can only be failed malloc) 0 if succeed*/
int insertElemToQ(char* dir_name);
/* handling error in thread- print and exit thread*/
void KillThread();
/* actions of a searching thread*/
int thread_search();

int killAllthreads = 0; // bollean parameter 

int main(int argc, char *argv[])
{
    //printf("MAIN");
    int return_value;
    DIR *open_dir;
    int i;
    thrd_t *thread_ids;
    DIR_element *first;

    /************** CHECK MAIN THREAD **************/
    /*Validate that the correct number of command line arguments is passed*/
    if (argc != 4)
    {
        fprintf(stderr, MAIN_ERROR_ARGC);
        exit(1);
    }
    //printf("1");
    /* Check if the directory specified in argv[1] can be searched */
    open_dir = opendir(argv[1]);
    if (open_dir == NULL)
    {
        fprintf(stderr, MAIN_ERROR_ARGV);
        exit(1);
    }
    // check if its return ERROR on NULL in argv[1]
    //printf("2");

    /* Create a FIFO queue that holds directories */
    dir_queue = (DIR_queue*)malloc(sizeof(DIR_queue));
    if (dir_queue == NULL)
    {
        fprintf(stderr, MAIN_ERROR_MALLOC);
        exit(1);
    }
    //printf("3");

    /* Put the search root directory in the queue*/
    first=(DIR_element*)malloc(sizeof(DIR_element));
    if (dir_queue == NULL)
    {
        fprintf(stderr, MAIN_ERROR_MALLOC);
        exit(1);
    }
    //printf("4");
    strcpy(first->dir_name,argv[1]);
    first->next=NULL;
    dir_queue->first=first;
    dir_queue->last=first;
    dir_queue->size=1;
    //printf("5");

    /* Initialize mutex and condition variable objects */
    return_value = mtx_init(&mutex,mtx_plain); // MIGHT NEED TO CHANGE mtx_plain
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
    //printf("6");

    term =argv[2];

    /* Create n(=argv[3]) searching threads */
    numOfThreads = atoi(argv[3]);
    thread_ids =malloc(numOfThreads * sizeof(thrd_t));
    if (thread_ids == NULL)
    {
        fprintf(stderr, MAIN_ERROR_MALLOC);
        exit(1);
    }
    //printf("8");

    mtx_lock(&allThreadsMutex);

    for (i = 0; i < numOfThreads; i++)
    {
        //printf("int loop");
        return_value = thrd_create(&thread_ids[i], thread_search,NULL);
        if (return_value != thrd_success)
        {
            fprintf(stderr, MAIN_ERROR_THREAD);
            exit(1);
        }
    }

    /*Wait for all other searching threads to be created and for the main thread to signal that the searching should start. */
    //mtx_lock(&mutex);
    while (numOfRunningThreads < numOfThreads)
    {
        /* waiting for threadCreated signal*/
        cnd_wait(&threadCreated, &allThreadsMutex);
    }
    //mtx_unlock(&mutex);

    // signal all threads that all threads were created
    cnd_broadcast(&allThreadsCreated);
    mtx_unlock(&allThreadsMutex);

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

    mtx_destroy(&mutex);
    mtx_destroy(&allThreadsMutex);

    free(first);
    free(thread_ids);

    printf("Done searching, found %d files\n", numMatchedFiles);
    if (!errorThread)
    {
        // no thread has encouterd an error
        exit(0);
    }
    exit(1);
}

int thread_search()
{
    //printf("in thread_search");
    int return_value;
    DIR *head_open; // for open head of queue directory
    char *entry_path; //saves path of a directory that will be added to queue
    char *head_path; // puts HEAD of queue in it
    DIR *searchable_dir; // check if current entry is a searchable directory
    char *check_term;

    struct dirent *entry;
    struct stat filestat;

    head_path=(char*)malloc(PATH_MAX * sizeof(char));
    if (head_path == NULL)
    {
        errorThread=1;
        fprintf(stderr,THREAD_ERROR_MALLOC);
        KillThread();
    }

    // update main that a thread was creating by update num of created threads and send a signal
    //mtx_lock(&mutex);
    mtx_lock(&allThreadsMutex);
    numOfRunningThreads++;
    cnd_broadcast(&threadCreated);
    // wait for all thread to be created
    cnd_wait(&allThreadsCreated, &allThreadsMutex);
    mtx_unlock(&allThreadsMutex);

    while (1)
    {
        printf("number matches: %d",numMatchedFiles);
        return_value=removeElemFromQ(head_path);
        if (return_value==1)
        {/* need to kill all threads*/
            KillThread();
        }

        printf("removed: %s\n",head_path);
        
        head_open = opendir(head_path);// should be able to read

        /* head_path is a searchable directory*/
        /* https://stackoverflow.com/questions/60535786/stat-using-s-isdir-dont-seem-to-always-work */
        /* iterate through each directory entry (dirent) in the directory obtained from the queue*/
        while ((entry = readdir(head_open)) != NULL)
        {
            //build full path
            entry_path = (char*)malloc(PATH_MAX * sizeof(char));
            if (entry_path == NULL)
            {
                errorThread=1;
                fprintf(stderr,THREAD_ERROR_MALLOC);
                KillThread();
            }
            strcpy(entry_path, head_path);
            strcat(entry_path, "/");
            strcat(entry_path, entry->d_name);

            stat(entry_path,&filestat);

            if( S_ISDIR(filestat.st_mode) )
            {// entry is a DIR
                if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 )
                {
                    continue;
                }
                else
                {
                    // check if dir is searchable
                    searchable_dir = opendir(entry_path);
                    if (searchable_dir == NULL)
                    {
                        continue;
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
                    mtx_lock(&mutex);
                    numMatchedFiles++;
                    mtx_unlock(&mutex);
                }
            }
            free(entry_path);
        }    
        closedir(head_open);
    }
}
/* insert new element to the end of the queue. return 1 if error occured (can only be failed malloc) 0 if succeed*/
int insertElemToQ(char* dir_name)
{
    printf("In Insert\n");
    DIR_element *dir_obj;
    dir_obj = (DIR_element*)malloc(sizeof(DIR_element));
    // malloc failed
    if (dir_obj == NULL)
    {
        return 1;
    }
    strcpy(dir_obj->dir_name, dir_name);
    mtx_lock(&mutex);
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
    /*send a signal that queue isn't empty*/
    cnd_broadcast(&isQueueEmpty);
    mtx_unlock(&mutex);
    printf("end Insert\n");
    return 0;
}

/*remove the head of queue and put it's dir_name in a given char*, 
if all threads need to be killed-return 1,else-did the job and return 0*/
int removeElemFromQ(char *dir_name)
{
    mtx_lock(&mutex);
    numOfThreadsWaiting++;
    /*Wait until the queue becomes non-empty */
    while (dir_queue->first == NULL)
    { 
        /*If all "alive" searching threads are waiting, that means there are no more
         directories to search- all searching threads should exit.*/
        if (numOfThreads == numOfThreadsWaiting+numOfThreadsKilled)
        {
            //killAllthreads = 1;
            numOfThreadsWaiting--;
            numOfThreadsKilled++;
            mtx_unlock(&mutex);
            cnd_broadcast(&isQueueEmpty);
            // exited when no thread encounterd an error
            thrd_exit(1);
        }
        else
        {
            /* queue is empty but not all threads are waiting- wait until it's not*/
            cnd_wait(&isQueueEmpty, &mutex);
            /*if(killAllthreads==1)
            {
                numOfThreadsWaiting--;
                mtx_unlock(&mutex);
                return 1;
            }*/
        }
    }
    /* current thread isn't waiting*/
    numOfThreadsWaiting--;

    DIR_element *rem_first;
    // dir_queue-> size!=0
    rem_first = dir_queue->first;
    strcpy(dir_name, rem_first->dir_name);
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
    mtx_unlock(&mutex);
    return 0;
}

void KillThread()
{
    mtx_lock(&mutex);
    numOfRunningThreads--;
    numOfThreadsKilled++;
    mtx_unlock(&mutex);
    thrd_exit(1);
}