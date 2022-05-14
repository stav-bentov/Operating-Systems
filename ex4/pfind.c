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
/* We have 3 condition that need to be checked*/
cnd_t allThreadsCreated; // The main thread use it to signal that the searching should start (all searching threads has been created).
cnd_t threadCreated;     // Use it to signal that a thread was created
cnd_t isQueueEmpty;      // if the queue is empty or added a directory

int numOfRunningThreads=0;// updated when any thread that is created or destroyed
int numOfThreads; // number of thread that should be created- given in argv[3]
int numOfThreadsWaiting = 0; // number of thread that are sleeping/waiting
int errorThread = 0; // boolean parameter- if an error occured in one of the thread- set 1
int numMatchedFiles; // number of files that were found under the given terms
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
    /* Check if the directory specified in argv[1] can be searched */
    open_dir = opendir(argv[1]);
    if (open_dir == NULL)
    {
        fprintf(stderr, MAIN_ERROR_ARGV);
        exit(1);
    }
    // check if its return ERROR on NULL in argv[1]

    /* Create a FIFO queue that holds directories */
    dir_queue = (DIR_queue*)malloc(sizeof(DIR_queue));
    if (dir_queue == NULL)
    {
        fprintf(stderr, MAIN_ERROR_MALLOC);
        exit(1);
    }

    /* Put the search root directory in the queue*/
    first=(DIR_element*)malloc(sizeof(DIR_element));
    if (dir_queue == NULL)
    {
        fprintf(stderr, MAIN_ERROR_MALLOC);
        exit(1);
    }
    strcpy(first->dir_name,argv[1]);
    first->next=NULL;
    dir_queue->first=first;
    dir_queue->last=first;
    dir_queue->size=1;

    /* Initialize mutex and condition variable objects */
    return_value = mtx_init(&mutex,mtx_plain); // MIGHT NEED TO CHANGE mtx_plain
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

    term =argv[2];

    /* Create n(=argv[3]) searching threads */
    numOfThreads = atoi(argv[3]);
    thread_ids =malloc(numOfThreads * sizeof(thrd_t));
    if (thread_ids == NULL)
    {
        fprintf(stderr, MAIN_ERROR_MALLOC);
        exit(1);
    }

    for (i = 0; i < numOfThreads; i++)
    {
        return_value = thrd_create(&thread_ids[i], thread_search,NULL);
        if (return_value != thrd_success)
        {
            fprintf(stderr, MAIN_ERROR_THREAD);
            exit(1);
        }
    }

    /*Wait for all other searching threads to be created and for the main thread to signal that the searching should start. */
    mtx_lock(&mutex);
    while (numOfRunningThreads < numOfThreads)
    {
        /* waiting for threadCreated signal*/
        cnd_wait(&threadCreated, &mutex);
    }
    mtx_unlock(&mutex);

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
    mtx_destroy(&mutex);
    printf("Done searching, found %d files\n", numMatchedFiles);
    if (!errorThread)
    {
        // no thread has encouterd an error
        exit(0);
    }
    exit(1);
}

void KillThread()
{
    mtx_lock(&mutex);
    numOfRunningThreads--;
    mtx_unlock(&mutex);
    thrd_exit(1);
}

int thread_search()
{
    printf("in thread search");
    int return_value;
    char *dir_curr_entry; // gets every entry of the removed directory
    char *new_dir; //saves path of a directory that will be added to queue

    DIR *open_dir;
    DIR *searchable_dir; // check if current entry is a searchable directory
    struct dirent *entry;
    struct stat filestat;

    char *dir_name; // puts HEAD of queue in it
    dir_name=(char*)malloc(PATH_MAX * sizeof(char));
    if (dir_name == NULL)
    {
        fprintf(stderr,THREAD_ERROR_MALLOC);
        KillThread();
    }
    char *check_term;

    // update main that a thread was creating by update num of created threads and send a signal
    mtx_lock(&mutex);
    numOfRunningThreads++;
    cnd_signal(&threadCreated);
    // wait for all thread to be created
    cnd_wait(&allThreadsCreated, &mutex);
    mtx_unlock(&mutex);

    while (1)
    {
        //printf("in loop\n");
        return_value=removeElemFromQ(dir_name);
        if (return_value==1)
        {/* need to kill all threads*/
            KillThread();
        }
        printf("removed: %s\n",dir_name);

        open_dir = opendir(dir_name);// should be able to read

        /* dir_name is a searchable directory*/
        /* https://c-for-dummies.com/blog/?p=3252 */
        /* iterate through each directory entry (dirent) in the directory obtained from the queue*/
        while ((entry = readdir(open_dir)) != NULL)
        {
            /* If the dirent is for a directory*/
            stat(entry->d_name, &filestat);
            if( S_ISDIR(filestat.st_mode) )
                printf("%4s: %s\n","Dir",entry->d_name);
            else
                printf("%4s: %s\n","File",entry->d_name);
            dir_curr_entry = entry->d_name; /*current entry*/

            /* If the name in the dirent is one of "." or "..", ignore it. */
            if (!strcmp(dir_curr_entry, ".") || !strcmp(dir_curr_entry, ".."))
            {
                continue;
            }
            if (S_ISDIR(filestat.st_mode))
            {
                // get full path
                new_dir = (char*)malloc(PATH_MAX * sizeof(char));
                if (new_dir == NULL)
                {
                    fprintf(stderr,THREAD_ERROR_MALLOC);
                    KillThread();
                }
                strcpy(new_dir, dir_name);
                strcat(new_dir, "/");
                strcat(new_dir, dir_curr_entry);

                printf("in dir : %s\n",new_dir);

                /* check if dir is searchable*/
                searchable_dir = opendir(new_dir);
                if (searchable_dir == NULL)
                {
                    continue;
                }
                else
                {
                    printf("searchable\n");
                    /* insert new directory path*/
                    return_value = insertElemToQ(new_dir);
                    printf("is segmention here?");
                    if (return_value==1)
                    {
                        fprintf(stderr,MAIN_ERROR_THREAD);
                        KillThread();
                    }
                    printf("is segmention here?");
                }
                closedir(searchable_dir);
                free(new_dir);
            }
            else
            { /* entry is a file- check the term*/
                check_term = strstr(dir_curr_entry, term);
                if (check_term != NULL)
                {
                    printf("%s/%s\n", dir_name, dir_curr_entry);
                    mtx_lock(&mutex);
                    numMatchedFiles++;
                    mtx_unlock(&mutex);
                }
            }
        }
        closedir(open_dir);
    }
}
/* insert new element to the end of the queue. return 1 if error occured (can only be failed malloc) 0 if succeed*/
int insertElemToQ(char* dir_name)
{
    printf("in insert\n");
    DIR_element *dir_obj;
    dir_obj = (DIR_element*)malloc(sizeof(DIR_element));
    // malloc failed
    if (dir_obj == NULL)
    {
        return 1;
    }
    printf("1\n");
    strcpy(dir_obj->dir_name, dir_name);
    mtx_lock(&mutex);
    // check if can be replaced with one of them!!!!!!!!!!!!!!!
    /* if queue is empty- need to set up first */
    if (dir_queue->first == NULL)
    {
        printf("2 here\n");
        dir_queue->first = dir_obj;
        dir_queue->last = dir_obj;
        dir_queue->size = 1;
    }
    else
    { // queue isn't empty
        printf("2\n");
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
    printf("3\n");
    /*send a signal that queue isn't empty*/
    cnd_broadcast(&isQueueEmpty);
    printf("4\n");
    mtx_unlock(&mutex);
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
        /*If all other searching threads are already waiting, that means there are no more
         directories to search- all searching threads should exit.*/
        if (numOfThreads == numOfThreadsWaiting)
        {
            killAllthreads = 1;
            numOfRunningThreads--;
            mtx_unlock(&mutex);
            cnd_broadcast(&isQueueEmpty);
            // exited when no thread ancounterd an error
            thrd_exit(EXIT_THREAD_NO_ERROR);
        }
        else
        {
            /* queue is empty but not all threads are waiting- wait until it's not*/
            cnd_wait(&isQueueEmpty, &mutex);
            if(killAllthreads==1)
                return 1;
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
