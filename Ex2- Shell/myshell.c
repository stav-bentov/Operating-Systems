#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

#define FORK_ERROR "ERROR- Failed forking"
#define EXECUTE_ERROR "ERROR- Failed executing command"
#define WAIT_ERROR "ERROR- Failed waiting a child's process"
#define PIPE_ERROR "ERROR- Failed piping"
#define FILE_ERROR "ERROR- Failed opening the file"
#define DUP_ERROR "ERROR- Failed redirecting the file"
#define SIGNAL_ERROR "ERROR- Failed changing signal properties"
#define READ 0
#define WRITE 1

/* =====================  Declarations ===================== */

/* Gets a signal (the signal's num) and make the process ignore it*/
int signal_IGN(int signal);

/* Gets a signal (the signal's num) and make it respond as usual  */
int signal_DFL(int signal);

/* Remove zombies and make SIGINT be ignored  */
int prepare(void);

/* Checks if a word is &,|,<< or none of them*/
int isSymbol(char *word);

/* Executing command- no special symbol, run the process as usual with fork() and execvp(), parent process should wait.
 * terminate foreground child upon SIGINT, return error if execvp doesn't succeed or if parent waiting failed.
 * if all good-return 1, else return 0*/
int execute_command(char **arglist);

/* Executing command in the background- the shell executes the command without wait for its completion
 before accepting another command. need to change & to null when passing arglist to execvp (using count).
 if all good-return 1, else return 0*/
int background_command(int count, char **arglist);


/* Output of first command is input of second command, parent process is waiting before accepting
 * another command. need to change | to null when passing arglist to execvp (using the index of it).
 if all good-return 1, else return 0*/
int piping_command(int index, char **arglist)

/* Output of the command is appended to the output file, parent process is waiting before accepting
 * another command. need to change >> to null when passing arglist to execvp (using the index of it).
 if all good-return 1, else return 0*/
int redirecting_command(int index, char **arglist);

int process_arglist(int count, char **arglist);
int finalize(void);

/* =====================  Functions ===================== */

int signal_IGN(int signal)
{
    struct sigaction sig;
    sig.sa_handler =SIG_IGN; // ignore signal
    if(sigaction(signal, &sig, NULL) == -1)
    {
        perror(SIGNAL_ERROR);
        return -1;
    }
    return 1;
}

int signal_DFL(int signal)
{
    struct sigaction sig;
    sig.sa_handler =SIG_DFL; //default signal action
    if(sigaction(signal, &sig, NULL) == -1)
    {
        perror(SIGNAL_ERROR);
        return -1;
    }
    return 1;
}

int prepare(void)
{
    // the child process entry is deleted from the process table when ignoring SIGCHLD- zombie isn't created
    if(signal_IGN(SIGCHLD)==-1)
    { //Error
        return -1;
    }

    // after prepare the parent won't terminate upon SIGINT (Handling of SIGINT 2)
    if(signal_IGN(SIGINT)==-1)
    { //Error
        return -1;
    }
    return 0;
}


int isSymbol(char *word){
    int b1=(*word == '&' || *word=='|') && strlen(word)==1;
    int b2=(word[0]=='>' && word[1]=='>') && strlen(word)==2;
    return b1 || b2;
}

int execute_command(char **arglist){
    pid_t pid = fork();
    if (pid == -1)
    { // Failed forking- error in parent process - so print error and return 0
        perror(FORK_ERROR);
        return 0;
    }
    else if (pid == 0)
    {// Child process
        /* Foreground child processes (regular commands or parts of a pipe) should terminate upon SIGINT.
        So SIGINT set to act as default*/
        if(signal_DFL(SIGINT)==-1)
        {// Error handling 4-5
            exit(1);
        }

        // execute process
        if (execvp(arglist[0],arglist) == -1)
        {
            // Error handling 4-5
            perror(EXECUTE_ERROR);
            exit(1);
        }
    }
    //(else) Parent process
    // Executing commands- waits until it completes before accepting another process
    int status;
    if (waitpid(pid,&status,0)==-1) 
    {
        // Error handling 3- if waitpid returns an error for ECHILD or EINTR don't exit the shell
        if(errno!=ECHILD && errno!=EINTR)
        {
            perror(WAIT_ERROR);
            return 0;
        }
        // No error occurs
    }
    return 1;
}

int background_command(int count, char **arglist)
{
    pid_t pid = fork();
    if (pid == -1)
    { // Failed forking- error in parent process - so print error and return 0
        perror(FORK_ERROR);
        return 0;
    }
    else if (pid == 0)
    {// Child process
        /* Background child processes should not terminate upon SIGINT.
         So SIGINT set to be ignored*/
        if(signal_IGN(SIGINT)==-1)
        {// Error handling 4-5
            exit(1);
        }

        /* Passing arglist to execvp without '&'- it is the last word of the command line
        so it's allocated in index count-1 in the array. */
        arglist[count-1]=NULL;

        // execute process
        if (execvp(arglist[0],arglist) == -1)
        {
            // Error handling 4-5
            perror(EXECUTE_ERROR);
            exit(1);
        }
    }
    // (else) Parent process- No need to wait
    return 1;
}

int piping_command(int index, char **arglist)
{
    // remove special symbol from arglist
    arglist[index]=NULL;

    int pipefd[2];
    if(pipe(pipefd)==-1)
    { // Failed piping
        perror(PIPE_ERROR);
        return 0;
    }

    pid_t pid1 = fork(); // (child) process 1 writing to (child) process 2
    if (pid1 == -1)
    { // Failed forking- error in parent process - so print error and return 0
        perror(FORK_ERROR);

        // close read and write 
        close(pipefd[0]);
        close(pipefd[1]);

        return 0;
    }
    else if (pid1 == 0)
    {// Child process- needs to WRITE 

        /* Foreground child processes (regular commands or parts of a pipe) should terminate upon SIGINT.
         So SIGINT set to act as default */
        if(signal_DFL(SIGINT)==-1)
        {// Error handling 4-5
            exit(1);
        }

        // Close read end
        close(pipefd[READ]);
        // replace STDOUT with new file descriptor 
        dup2(pipefd[WRITE],STDOUT_FILENO);
        // we used dup2- now we dont need it
        close(pipefd[WRITE]);

        // execute process
        if (execvp(arglist[0],arglist) == -1)
        {
            // Error handling 4-5
            perror(EXECUTE_ERROR);
            exit(1);
        }
    }
    // (else) Parent process- creates a new child process that will read from the pipe
    pid_t pid2 = fork(); // (child) process 2 reading from (child) process 1
    if (pid2 == -1)
    { // Failed forking- error in parent process - so print error and return 0
        perror(FORK_ERROR);

        // close read and write 
        close(pipefd[0]);
        close(pipefd[1]);

        return 0;
    }
    else if (pid2 == 0)
    {
        /* Foreground child processes (regular commands or parts of a pipe) should terminate upon SIGINT.
        So SIGINT set to act as default */
        if(signal_DFL(SIGINT)==-1)
        {// Error handling 4-5
            exit(1);
        }

        // Close read end
        close(pipefd[WRITE]);
        // replace STDIN with new file descriptor 
        dup2(pipefd[READ],STDIN_FILENO);
        // we used dup2- now we dont need it
        close(pipefd[READ]);

        // execute process
        if (execvp(arglist[index+1],&arglist[index+1]) == -1)
        {
            // Error handling 4-5
            perror(EXECUTE_ERROR);
            exit(1);
        }
    }
    // (else) Parent- close read and write and waits for both processes 
    close(pipefd[WRITE]);
    close(pipefd[READ]);

    int status;
    if (waitpid(pid1,&status,0)==-1 || waitpid(pid2,&status,0)==-1) {
        // Error handling 3- if waitpid returns an error for ECHILD or EINTR don't exit the shell
        if(errno!=ECHILD && errno!=EINTR)
        {
            perror(WAIT_ERROR);
            return 0;
        }
    }
    // No error occurs
    return 1;
}

int redirecting_command(int index, char **arglist)
{
    // remove spacial symbol from arglist
    arglist[index]=NULL;

    //int fileDesc=open(arglist[index+1] ,O_WRONLY | O_CREAT | O_APPEND | O_TRUNC |);

    /* open the file: O_CREAT = if it doesnt exist then creates it,
        O_APPEND= allows to append the output to the file,
        O_WRONLY= open for writing only 
        answer from https://stackoverflow.com/questions/10705612/how-to-open-a-file-in-append-mode-using-open-fopen*/
    int fileDesc=open(arglist[index+1] ,O_CREAT|O_APPEND|O_WRONLY,0600);
    if(fileDesc==-1)
    {// Failed opening/creating a file
        perror(FILE_ERROR);
        return 0;
    }

    pid_t pid = fork(); // (child) process 1 writing to (child) process 2
    if (pid == -1)
    { // Failed forking- error in parent process - so print error and return 0
        perror(FORK_ERROR);
        return 0;
    }
    else if (pid == 0)
    {// Child process- needs to WRITE to the file
        // Foreground child processes (regular commands or parts of a pipe) should terminate upon SIGINT.
        // So SIGINT set to act as default
        if(signal_DFL(SIGINT)==-1)
        {// Error handling 4-5
            exit(1);
        }

        // replace STDOUT with new file descriptor 
        if(dup2(fileDesc,STDOUT_FILENO)==-1)
        {// Error handling 4-5, duplicate doen't sucseed 
            perror(DUP_ERROR);
            exit(1);
        }

        close(fileDesc);

        // execute process
        if (execvp(arglist[0],arglist) == -1)
        {
            // Error handling 4-5
            perror(EXECUTE_ERROR);
            exit(1);
        }
    }
    // (else) Parent process
    // Executing commands- waits until it completes before accepting another process
    int status;
    if (waitpid(pid,&status,0)==-1) {
        // Error handling 3- if waitpid returns an error for ECHILD or EINTR don't exit the shell
        if(errno!=ECHILD && errno!=EINTR)
        {
            perror(WAIT_ERROR);
            return 0;
        }
    }
    // No error occurs
    return 1;
}

int process_arglist(int count, char **arglist)
{
    int i;
    int breakFor = 0;

    // search for &,>>,| (or none of them)
    for (i=0; i < count; i++)
    {
        // arg[i]=& (or) >> (or) |
        if (isSymbol(arglist[i]))
        {
            breakFor = 1;
            break;
        }
    }

    if (breakFor) {
        char symbol = *arglist[i];
        switch (symbol) {
            case '&':
                return background_command(count,arglist);
                break;
            case '|':
                return piping_command(i,arglist);
                break;
            case '>': // if the symbol is '>>' then it's the right case
                return redirecting_command(i,arglist);
                break;
            default:// never get into it because isSymbol is true, so it's one of the 3 cases
                break;
        }
    }
    // (else)- regular command
    return execute_command(arglist);
}

int finalize(void)
{
    return 0;
}