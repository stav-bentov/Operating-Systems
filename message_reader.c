#define NUM_OF_ARGUMENT_ERROR "ERROR: Number of arguments isn't correct!"
#define OPEN_FILE_ERROR "ERROR: open the specified message slot device file failed!"
#define IOCTL_ERROR "ERROR: invoking ioctl() failed!"
#define READ_ERROR "ERROR: reading failed!"
#define WRITE_ERROR "ERROR: print message to standard output failed!"

#include <fcntl.h>      /* open */ 
#include <unistd.h>     /* exit */
#include <sys/ioctl.h>  /* ioctl */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "message_slot.h"

int main(int argc, char *argv[])
{
    int msg_file;
    char *msg_path;
    unsigned int channel_id;
    char msg[BUFFER_BOUND];
    int return_value;
    int msg_length;
    
    if(argc==3)
    {
        msg_path=argv[1];
        channel_id=atoi(argv[2]);

        msg_file=open(msg_path,O_RDWR);
        if(msg_file<0)
        {
            perror(OPEN_FILE_ERROR);
            exit(1);
        }

        return_value=ioctl(msg_file,MSG_SLOT_CHANNEL,channel_id);
        if(return_value!=SUCCESS)
        {
            close(msg_file);
            perror(IOCTL_ERROR);
            exit(1);
        }

        return_value=read(msg_file,msg,BUFFER_BOUND);
        if(return_value<0)
        {
            close(msg_file);
            perror(READ_ERROR);
            exit(1);
        }
        close(msg_file);
        
        msg_length=return_value;
        return_value=write(STDOUT_FILENO, msg,msg_length);
        /*On success, the number of bytes written is returned*/
        if(return_value!=msg_length)
        {
            perror(WRITE_ERROR);
            exit(1);
        }
        exit(0);
    }
    perror(NUM_OF_ARGUMENT_ERROR);
    exit(1);
}
