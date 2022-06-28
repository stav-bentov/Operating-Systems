// got help from https://www.geeksforgeeks.org/tcp-server-client-implementation-in-c/
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
 #include <endian.h>
#include <stdatomic.h>

#define ARGC 2
#define QUEUE_SIZE 10
#define UPPER_B_PRINTABLE 126
#define LOWER_B_PRINTABLE 32
#define NUM_BYTES_64INT 8

#define ARGS_ERROR "Server ERROR: incorrect number of command line arguments is passed"
#define SIGACTION_ERROR "Server ERROR: failed sigaction"
#define SOCKET_ERROR "Server ERROR: failed creating socket"
#define BIND_ERROR "Server ERROR: failed binding"
#define LISTEN_ERROR "Server ERROR: failed listening"
#define ACCEPT_ERROR "Server ERROR: failed accepting"
#define READ_N_ERROR "Server ERROR: failed reading N from client"
#define READ_N_TCP_ERROR "Server ERROR: TCP error occured while reading N from client"
#define READ_K_ERROR "Server ERROR: client process was killed unexpectedly while reading N from client"
#define READ_FILE_ERROR "Server ERROR: failed reading the file from client"
#define READ_FILE_TCP_ERROR "Server ERROR: TCP error occured while reading the file from client"
#define READ_K_FILE_ERROR "Server ERROR: client process was killed unexpectedly while reading the file from client"
#define WRITE_SIZE_ERROR "Server ERROR: failed writing C to client"
#define WRITE_K_SIZE_ERROR "Server ERROR: client process was killed unexpectedly while writing C to client"
#define WRITE_TCP_SIZE_ERROR "Server ERROR: TCP error occured while writing C to client"


/*-------------------Declarations-------------------*/
uint64_t pcc_total[UPPER_B_PRINTABLE - LOWER_B_PRINTABLE + 1];//Will count how many times each printable characterwas observed in all client connections
int connection_error=0; // TCP errors or unexpected connection close- connection_error=1
int SIGINT_mood=0;
int client_procced=0; //0 - no client is being procceed, 1- a client is beeing procceed
atomic_int connfd=-1;// when the server is proccesing a client connfd=accept(), else- connfd=-1 (in case of an error too)

/* passing a condition of an error in bool- if true it's an error- print it and exit(1)*/
void error_occured_exit(int bool, char *error_msg);

/* passing a condition of an error in bool- if true it's a TCP error or connection termineted- connection_error=1 */
void error_occured_not_exit(int bool, char *error_msg1,char *error_msg2);

/* while the server in “processing a client”- update SIGINT_mood=1 so when this process ends it will be the last, and prints statistics and exit
else- print statistics and exit */
void SIGINT_action();

/* A printable character is a byte b whose value is 32 ≤ b ≤ 126*/
int is_printable(char c);

void print_pcc_total();

/*-------------------Functions-------------------*/
int main(int argc, char *argv[])
{
    int return_value;
    int sockfd;
    uint16_t server_port;
    int current_count_pc; // Count number of printable characters was observed in one client connection

    // For getting N
    int count_int_bytes = NUM_BYTES_64INT;
    int count_unread_bytes; // Number of bytes left to read
    int count_read_bytes;// Number of bytes read
    int count_unwritten_bytes;// Number of bytes left to write
    int count_written_bytes;// Number of bytes written
    int i;

    // ================Set arguments and check================
    error_occured_exit(argc != ARGC, ARGS_ERROR);
    server_port = atoi(argv[1]);

    memset(pcc_total, 0, (UPPER_B_PRINTABLE - LOWER_B_PRINTABLE+1) * sizeof(uint64_t));

    // ===================Set Handle SIGINT===================
    struct sigaction control_SIGINT;
    control_SIGINT.sa_handler=&SIGINT_action;
    control_SIGINT.sa_flags=SA_RESTART;
    return_value=sigaction(SIGINT,&control_SIGINT,NULL);
    error_occured_exit(return_value==-1,SIGACTION_ERROR);

    // ===========Listen to incoming TCP connections==========
    struct sockaddr_in server_address;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    error_occured_exit(sockfd == -1, SOCKET_ERROR);

    //Got from https://stackoverflow.com/questions/24194961/how-do-i-use-setsockoptso-reuseaddr
    int enableS =1;
    return_value=setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enableS, sizeof(int));
    error_occured_exit(return_value == -1, SOCKET_ERROR);

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(server_port); 
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);//The socket will be bound to all local interfaces

    return_value = bind(sockfd, (struct sockaddr *)&server_address, sizeof(server_address));
    error_occured_exit(return_value == -1, BIND_ERROR);

    return_value = listen(sockfd, QUEUE_SIZE);
    error_occured_exit(return_value == -1, LISTEN_ERROR);


    // ===================Connection accepted===================
    while (1)
    {
        uint64_t result; // Keeps N as network byte order
        current_count_pc=0;
        count_unread_bytes = count_int_bytes;
        count_read_bytes = 0;
        count_unwritten_bytes = 8;// Number of bytes left to write
        count_written_bytes = 0;// Number of bytes written
        uint64_t N;
        uint64_t C;

        // ====================================PROCESSING CLIENT====================================
        connfd = accept(sockfd, NULL, NULL);
        error_occured_exit(connfd == -1, ACCEPT_ERROR);

        // ====================================Get N from client====================================
        while (count_read_bytes < count_int_bytes && !connection_error)
        {
            return_value = read(connfd, &result + count_read_bytes, count_unread_bytes);
            if(return_value==0 && count_read_bytes!=count_int_bytes)
            {// "the client process is killed unexpectedly"
                fprintf(stderr, "%s: %s", READ_K_ERROR, strerror(errno));
                connection_error=1;
            }
            else
            {
                error_occured_not_exit(return_value == -1, READ_N_TCP_ERROR,READ_N_ERROR);
            }

            // retrun_value is valid -update counters
            if(!connection_error)
            {
                count_unread_bytes -= return_value;
                count_read_bytes += return_value;
            }
        }

        // TCP error or client process was killed unexpectedly
        if(connection_error)
        {
            // No need to handle added data because no data was inserted yet
            close(connfd);
            connfd=-1;
            if(SIGINT_mood)
            {
                print_pcc_total();
            }
            continue;
        }

        N = be64toh(result);

        // =================================DONE- Get N from client=================================

        // Creates a temporary pcc_total for a current client connection 
        uint64_t current_pcc_total[UPPER_B_PRINTABLE - LOWER_B_PRINTABLE + 1];
        memset(current_pcc_total,0,(UPPER_B_PRINTABLE - LOWER_B_PRINTABLE + 1)*sizeof(uint64_t));

        // ==================================Read file from client==================================
        count_unread_bytes = N;
        count_read_bytes = 0;
        while (count_read_bytes < N && !connection_error)
        {
            char c; // read every while loop one char
            int index;

            return_value = read(connfd, &c, sizeof(char));
            if(return_value==0 && count_read_bytes!=N)
            {// "the client process is killed unexpectedly"
                fprintf(stderr, "%s: %s", READ_K_FILE_ERROR, strerror(errno));
                connection_error=1;
            }
            else
            {
                error_occured_not_exit(return_value == -1, READ_FILE_TCP_ERROR,READ_FILE_ERROR);
            }

            // retrun_value is valid
            if(!connection_error)
            {
                // Update counters
                count_unread_bytes -= return_value;
                count_read_bytes += return_value;

                // Check if printable and update data
                if(is_printable(c))
                {
                    current_count_pc++;
                    index=(int)(c)-LOWER_B_PRINTABLE;
                    current_pcc_total[index]++;
                }

            }
        }

        if(connection_error)
        {
            close(connfd);
            connfd=-1;
            if(SIGINT_mood)
            {
                print_pcc_total();
            }
            continue;
        }

        // ============================Write current_count_pc to client=============================
        C = htobe64(current_count_pc);
        count_unwritten_bytes = count_int_bytes;
        count_written_bytes = 0;
        
        while (count_written_bytes<count_int_bytes && !connection_error)
        {
            // each byte is starting from (&N+count_sent_bytes)
            // TODO check if count_unsent_bytes correct
            return_value = write(connfd, &C + count_written_bytes, count_unwritten_bytes);
            
            if(return_value==0 && count_written_bytes!=count_int_bytes)
            {// "the client process is killed unexpectedly"
                fprintf(stderr, "%s: %s", WRITE_K_SIZE_ERROR, strerror(errno));
                connection_error=1;
            }
            else
            {
                error_occured_not_exit(return_value == -1, WRITE_TCP_SIZE_ERROR,WRITE_SIZE_ERROR);
            }
            
            if(!connection_error)
            {
                count_unwritten_bytes -= return_value;
                count_written_bytes += return_value;
            }
        }

        if(connection_error)
        {
            close(connfd);
            connfd=-1;
            if(SIGINT_mood)
            {
                print_pcc_total();
            }
            continue;
        }
        
        // Else- update pcc_total
        for(i=0;i<(UPPER_B_PRINTABLE - LOWER_B_PRINTABLE + 1);i++)
        {
            pcc_total[i]=pcc_total[i]+current_pcc_total[i];
        }

        if(SIGINT_mood)
        {
            close(connfd);
            connfd=-1;
            print_pcc_total();
        }

        close(connfd);
        connfd=-1;
    }

    exit(0);
}

void SIGINT_action()
{
    // The server is not processing a client when SIGINT is delivered
    if(connfd<0)
    {
        print_pcc_total();
        exit(0);
    }
    // else- The server is processing a client when SIGINT is delivered
    SIGINT_mood=1;
}

void print_pcc_total()
{
    int i;
    for(i=0;i<(UPPER_B_PRINTABLE - LOWER_B_PRINTABLE + 1);i++)
    {
        printf("char '%c' : %lu times\n",(i+LOWER_B_PRINTABLE),pcc_total[i]);
    }
    exit(0);
}

int is_printable(char c)
{ 
    // (A printable character is a byte b whose value is 32 ≤ b ≤ 126)
    return (c<=UPPER_B_PRINTABLE && c>=LOWER_B_PRINTABLE);
}

void error_occured_exit(int bool, char *error_msg)
{
    if (bool)
    {
        fprintf(stderr, "%s: %s", error_msg, strerror(errno));
        exit(1);
    }
}

void error_occured_not_exit(int bool, char *error_msg1,char *error_msg2)
{
    if (bool)
    { // error occured check which type
        if(errno==ETIMEDOUT || errno==ECONNRESET || errno== EPIPE)
        { // TCP errors
            fprintf(stderr, "%s: %s", error_msg1, strerror(errno));
            connection_error=1;
        }
        else
        {
            fprintf(stderr, "%s: %s", error_msg2, strerror(errno));
            exit(1);
        }
    }
}
