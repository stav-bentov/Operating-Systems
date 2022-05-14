#ifndef CHARDEV_H
#define CHARDEV_H

#include <linux/ioctl.h>

#define MAJOR_NUM 235
#define SUCCESS 0
#define DEVICE_RANGE_NAME "message_slot"
#define BUFFER_BOUND 128
#define DEVICE_FILE_BOUND 256

// ioctl command-----------------like in rection =check this!!!!
#define MSG_SLOT_CHANNEL _IOW(MAJOR_NUM, 0, unsigned long)

#endif