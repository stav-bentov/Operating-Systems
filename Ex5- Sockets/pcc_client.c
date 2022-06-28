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

#define ARGC 4
#define BUF_MAX_SIZE 1000000
#define NUM_BYTES_64INT 8

#define ARGS_ERROR "Client ERROR: incorrect number of command line arguments is passed"
#define OPEN_FILE_ERROR "Client ERROR: failed opening given file"
#define SOCKET_ERROR "Client ERROR: failed creating socket"
#define CONNECT_ERROR "Client ERROR: failed connecting"
#define WRITE_FILE_TO_SERVER_ERROR "Client ERROR: failed writing file to server"
#define WRITE_SIZE_TO_SERVER_ERROR "Client ERROR: failed writing file-size to server"

/*-------------------Declarations-------------------*/
/* Passing a condition of an error in bool- if true it's an error- print it and exit(1)*/
void error_occured(int bool, char *error_msg);

/* Reads every while loop 1MB- by passing it to buffer and then sents it to server*/
void write_file_to_server(int sockfd, int file_desc);

/* Given 64 bit number convert and write to socket)*/
void write_size_to_server(uint64_t file_size, int sockfd);

/* Read C that was sent from server*/
void read_from_server(int sockfd);

/*-------------------Functions-------------------*/
int main(int argc, char *argv[])
{
    int return_value;
    int sockfd;
    uint64_t file_size;

    // ================Set arguments and check================
    error_occured(argc != ARGC, ARGS_ERROR);

    char *server_IP = argv[1];
    unsigned int server_port = atoi(argv[2]);
    char *file_path = argv[3];

    int file_desc = open(file_path, O_RDONLY);
    error_occured(file_desc == -1, OPEN_FILE_ERROR);

    // ====================Make Connection====================
    struct sockaddr_in server_address;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    error_occured(sockfd == -1, SOCKET_ERROR);

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(server_port); // Note: htons for endiannes
    server_address.sin_addr.s_addr = inet_addr(server_IP);

    return_value = connect(sockfd, (struct sockaddr *)&server_address, sizeof(server_address));
    error_occured(return_value == -1, CONNECT_ERROR);

    // Got from https://stackoverflow.com/questions/238603/how-can-i-get-a-files-size-in-c
    struct stat st;
    stat(file_path, &st);
    file_size = (uint64_t) st.st_size;

    // Writes (N) to server
    write_size_to_server(file_size, sockfd);

    // Writes file to server
    write_file_to_server(sockfd, file_desc);

    // Reads (C) from server
    read_from_server(sockfd);

    close(file_desc);

    exit(0);
}

void write_size_to_server(uint64_t file_size, int sockfd)
{
    // N- the number of bytes that will be transferred (file_size in network byte order)
    uint64_t N = htobe64(file_size);
    int count_int_bytes = NUM_BYTES_64INT;// There are 64/8=8 bytes to send to server
    int count_unwritten_bytes = NUM_BYTES_64INT;// Number of bytes left to write
    int count_written_bytes = 0;// Number of bytes written
    int return_value;
    
    while (count_written_bytes < count_int_bytes)
    {
        // each byte is starting from (&N+count_sent_bytes)
        return_value = write(sockfd, &N + count_written_bytes, count_unwritten_bytes);
        error_occured(return_value == -1, WRITE_SIZE_TO_SERVER_ERROR);
        count_unwritten_bytes -= return_value;
        count_written_bytes += return_value;
    }
}

void write_file_to_server(int sockfd, int file_desc)
{
    char *buffer[BUF_MAX_SIZE];
    int current_bytes_read;
    int current_bytes_sent;

    while ((current_bytes_read = read(file_desc, buffer, BUF_MAX_SIZE)) != 0)
    {
        current_bytes_sent = write(sockfd, buffer, current_bytes_read);
        error_occured(current_bytes_sent == -1, WRITE_FILE_TO_SERVER_ERROR);
    }
}

void read_from_server(int sockfd)
{
    uint64_t result; // C
    int retrun_value;

    int count_int_bytes = NUM_BYTES_64INT;// There are 64/8=8 bytes to read from server
    int count_unread_bytes = NUM_BYTES_64INT;// Number of bytes left to read
    int count_read_bytes = 0;// Number of bytes read
    while (count_read_bytes < count_int_bytes)
    {
        retrun_value = read(sockfd, &result + count_read_bytes, count_unread_bytes);
        error_occured(retrun_value == -1, WRITE_SIZE_TO_SERVER_ERROR);
        count_unread_bytes -= retrun_value;
        count_read_bytes += retrun_value;
    }

    uint64_t server_count = be64toh(result);

    close(sockfd);

    printf("# of printable characters: %lu\n", server_count);
}

void error_occured(int bool, char *error_msg)
{
    if (bool)
    {
        fprintf(stderr, "%s: %s", error_msg, strerror(errno));
        exit(1);
    }
}
