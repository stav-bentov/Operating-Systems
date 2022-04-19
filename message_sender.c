#define NUM_OF_ARGUMENT_ERROR "ERROR: Number of arguments isn't correct!\n"
#define INVALID_ID_ERROR "ERROR: the target channel id isn't a non-negative integer!\n"
#define OPEN_FILE_ERROR "ERROR: open the specified message slot device file failed!\n"
#define IOCTL_ERROR "ERROR: invoking ioctl() failed!\n"
#define WRITE_ERROR "ERROR: writing failed!\n"

#include <fcntl.h>      /* open */ 
#include <unistd.h>     /* exit */
#include <sys/ioctl.h>  /* ioctl */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "message_slot.h"

int main(int argc, char const *argv[])
{
    int msg_file;
    char *msg_path;
    unsigned int channel_id;
    char *msg;
    int return_value;
    
    if(argc==4)
    {
        msg_path=argv[1];
        channel_id=atoi(argv[2]);
        msg=argv[3];

        if(channel_id<=0)
        {
            perror(INVALID_ID_ERROR);
            exit(1);
        }

        msg_file=open(msg_path,O_RDWR);
        /*check*/
        if(msg_file<0)
        {
            perror(OPEN_FILE_ERROR);
            exit(1);
        }

        return_value=ioctl(msg_file,MSG_SLOT_CHANNEL,channel_id);
        if(return_value!=0)
        {
            perror(IOCTL_ERROR);
            close(msg_file);
            exit(1);
        }

        return_value=write(msg_file,msg,strlen(msg));
        if(return_value<0)
        {
            perror(WRITE_ERROR);
            close(msg_file);
            exit(1);
        }
        close(msg_file);
        exit(0);
    }
    perror(NUM_OF_ARGUMENT_ERROR);
    exit(1);
}
