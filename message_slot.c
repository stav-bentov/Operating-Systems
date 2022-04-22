/* part of the code was taken from chardev.c that we've seen in the recitation*/
#undef __KERNEL__
#define __KERNEL__
#undef MODULE
#define MODULE

#include <linux/kernel.h>  /* We're doing kernel work */
#include <linux/module.h>  /* Specifically, a module */
#include <linux/fs.h>      /* for register_chrdev */
#include <linux/uaccess.h> /* for get_user and put_user */
#include <linux/string.h>  /* for memset. NOTE - not string.h!*/
#include "message_slot.h"
#include <linux/slab.h>
#include <linux/errno.h>

MODULE_LICENSE("GPL");

/* the channel will act like a linked list- every channel will be connect to next one,
every channel has his own id, message and the length of it*/
typedef struct message_channel
{
  unsigned int id;                              /* channel's id */
  char *msg;                                    /* current message */
  int msg_length;                               /* message's length */
  struct message_channel *next_message_channel; /* pointer to next channel */
} message_channel;

/* represents the message-slot-file. points to it's channels*/
typedef struct message
{
  message_channel *first_channel;   /* pointer to the first messege channel */
  message_channel *current_channel; /* pointer to the last used messege channel */
  int first_set;
} message;

/* "there can be at most 256 different message slots device files"
so we'll keep an array that will represent 256 device files, each of them
will point to his first channel*/
static message *device_file_array[DEVICE_FILE_BOUND+1];

//================== DEVICE FUNCTIONS ===========================
static int device_open(struct inode *inode,
                       struct file *file)
{
  int minor; /*device file minor number*/
  message *current_msg; /*device file minor number*/
  message_channel *channel; 

  printk(KERN_ALERT "in device_open\n");

  minor = iminor(inode);
  printk(KERN_DEBUG "Minor =%d\n", minor);

  /* there is no structure for the file being opened- so create one and sets pointer to it*/
  if (device_file_array[minor] == NULL)
  {
    current_msg = (message*)kmalloc(sizeof(message), GFP_KERNEL);
    channel = (message_channel*)kmalloc(sizeof(message_channel), GFP_KERNEL);
    /* failed kmalloc*/
    if (current_msg == NULL || channel == NULL)
    {
      printk(KERN_DEBUG "current_msg == NULL || channel == NULL\n");
      return -1;
    }
    /*else, both allocations succeed- fill memory with zero*/

    //memset(current_msg, 0, sizeof(message));
    //memset(channel, 0, sizeof(message_channel));
    current_msg->first_channel = channel;
    current_msg->first_set = 0;

    device_file_array[minor] = current_msg;
    printk(KERN_DEBUG "is device_file_array[minor]==NULL (should be 0)= %d\n",device_file_array[minor] == NULL);
  }
  return SUCCESS;
}

//---------------------------------------------------------------
static int device_release(struct inode *inode,
                          struct file *file)
{
  return SUCCESS;
}

static long device_ioctl(struct file *file,
                         unsigned int ioctl_command_id,
                         unsigned long ioctl_param)
{
  int minor;
  message *current_msg; /* file's message*/
  message_channel *channel_pointer; /* pointer to a channel- we'll use it to search the channel according to channel id and create it if needed*/
  message_channel *new_channel,*prev_channel;
  int channel_exist; 
  channel_exist=0; /*channel doesn't exsit*/
  prev_channel=NULL;

  printk(KERN_ALERT "in device_ioctl\n");

  if (ioctl_command_id != MSG_SLOT_CHANNEL || ioctl_param <= 0)
  {
    printk(KERN_DEBUG "ioctl_command_id != MSG_SLOT_CHANNEL || ioctl_param <= 0\n");
    /* The c library interprets this and gives -1 return and set errno to EINVAL*/
    return -EINVAL;
  }

  printk(KERN_DEBUG "Channel ID= %ld\n", ioctl_param);

  
  if((file->f_inode)==NULL)
  {
    return-EINVAL;
  }
  
  minor=iminor(file->f_inode);
  file->private_data = (void*)device_file_array[minor]; /* the file now has a pointer to the message slot file*/

  /* get the message of the file from private data-field where we saved a pointer
  to it and find the channel according to the given id or create one*/
  current_msg = (message*)(file->private_data);
  printk(KERN_DEBUG "current_msg= %p\n", current_msg);

  if(current_msg==NULL)
  {
    printk(KERN_DEBUG "file descriptor isn't assosiated well (file->private_data is NULL)\n");
    return -1;
  }

  channel_pointer = (message_channel*)(current_msg->first_channel);
  printk(KERN_DEBUG "channel_pointer= %p\n", channel_pointer);

  /* first channel has been set- search for the channel with id= octl_param*/
  if(current_msg->first_set)
  {
    printk(KERN_DEBUG "first channel exsits\n");
    while(channel_pointer!=NULL)
    {
      /*channel exist*/
      if(channel_pointer->id==ioctl_param)
      {
        printk(KERN_DEBUG "channel exsits\n");
        current_msg->current_channel=channel_pointer;
        channel_exist=1;
        break;
      }
      prev_channel=channel_pointer;
      channel_pointer=channel_pointer->next_message_channel;
    }
  }
  /* need to create new channel*/
  if(!current_msg->first_set || !channel_exist)
  {
    /* create the new channel*/
    printk(KERN_DEBUG "create new channel\n");
    new_channel = (message_channel*)kmalloc(sizeof(message_channel), GFP_KERNEL);
    /* failed kmalloc*/
    if (new_channel == NULL)
    {
      printk(KERN_DEBUG "malloc didnt secseed\n");
      return -1;
    }

    /*else, allocation succeed- fill memory with zero*/
    //memset(new_channel, 0, sizeof(message_channel));
    new_channel->id=ioctl_param;

    /* first channel isn't set- new channel needs to be first channel*/
    if(!current_msg->first_set)
    {
      printk(KERN_DEBUG "first channel isn't exsit\n");
      current_msg->first_channel = new_channel;
      current_msg->first_set=1;
    }
    else
    {
      printk(KERN_DEBUG "add chaneel to last\n");
      /* add new channel to the end of the "channel list"*/
      prev_channel->next_message_channel=new_channel;
    }
    /* add new channel and update current_channel*/
    current_msg->current_channel = new_channel;
  }
  printk(KERN_DEBUG "check pointer:\n");
  printk(KERN_DEBUG "current_msg->current_channel= new channel= %p:\n",current_msg->current_channel);
  printk(KERN_DEBUG "current_msg->first_channel=%p:\n",current_msg->first_channel);
  printk(KERN_DEBUG "check (message*)file->private_data:\n");
  printk(KERN_DEBUG "(message*)file->private_data= %p:\n",(message*)file->private_data);
  printk(KERN_DEBUG "(message*)file->private_data>current_channel= new channel= %p:\n",((message*)(file->private_data))->current_channel);
  printk(KERN_DEBUG "(message*)file->private_data->first_channel=%p:\n",((message*)(file->private_data))->first_channel);

  printk(KERN_DEBUG "returned value= %d\n",SUCCESS);

  return SUCCESS;
}

static ssize_t device_write(struct file *file,
                            const char __user *buffer,
                            size_t length,
                            loff_t *offset)
{
  message *current_msg; /* file's message*/
  message_channel *channel; /* message's current channel*/
  char *content_msg; /* pointer that will help in writing the data from buffer to channel */
  int i;

  printk(KERN_ALERT "in device_write\n");

  if(file==NULL)
  {
    printk(KERN_DEBUG "file==NULL\n");
    return -EINVAL;
  }
  
  current_msg = (message*)(file->private_data);

  /* no message has been set to file*/
  if (current_msg == NULL)
  {
    printk(KERN_DEBUG "current_msg == NULL\n");
    /* The c library interprets this and gives -1 return and set errno to EINVAL*/
    return -EINVAL;
  }
  printk(KERN_DEBUG "current_msg == %p\n",current_msg);

  channel = current_msg->current_channel;
  
  /* no channel has been set to message*/
  if (channel == NULL)
  {
    printk(KERN_DEBUG "channel == NULL\n");
    /* The c library interprets this and gives -1 return and set errno to EINVAL*/
    return -EINVAL;
  }
  printk(KERN_DEBUG "channel == %p\n",channel);

  /* the message's length is 0 or more then 128*/
  if (length == 0 || length > BUFFER_BOUND)
  {
    printk(KERN_DEBUG "length == 0 || length > BUFFER_BOUND\n");
    /* The c library interprets this and gives -1 return and set errno to EMSGSIZE*/
    return -EMSGSIZE;
  }

  /* allocate memory to the message content if needed*/
  if (channel->msg == NULL)
  {
    printk(KERN_DEBUG "channel->msg == NULL\n");
    channel->msg = (char *)kmalloc(BUFFER_BOUND, GFP_KERNEL);
    /* failed kmalloc*/
    if (channel == NULL)
    {
      return -1;
    }
  }

  /* reset prievious message or set up the message in the channel*/
  content_msg = channel->msg;
  memset(content_msg, 0, sizeof(BUFFER_BOUND));

  channel->msg_length = length;
  /* get the message from buffer and update channel content*/
  for (i = 0; i < length; i++)
  {
    /* get_user returns 0 on success*/
    if ((get_user(content_msg[i], &buffer[i])) != 0)
    {
      /* The c library interprets this and gives -1 return and set errno to EFAULT*/
      return -1;
    }
  }
  
  printk(KERN_DEBUG "is channel_msg==null? (should be 0)= %p:\n",(((message*)(file->private_data))->current_channel)->msg);
  printk(KERN_DEBUG "current_msg %p:\n",(message*)(file->private_data));
  printk(KERN_DEBUG "channel->msg == %d\n",channel->msg_length);

  return length;
}

static ssize_t device_read(struct file *file,
                           char __user *buffer,
                           size_t length,
                           loff_t *offset)
{
  message *current_msg; /* file's message*/
  message_channel *channel; /* message's current channel*/
  char *content_msg; /* content that will be written in channel */
  int i;

  printk(KERN_ALERT "in device_read\n");

  if(file==NULL)
  {
    printk(KERN_DEBUG "file==NULL\n");
    return -EINVAL;
  }

  current_msg = (message*)file->private_data;
  
  /* no message has been set to file*/
  if (current_msg == NULL)
  {
    printk(KERN_DEBUG "current_msg == NULL\n");
    /* The c library interprets this and gives -1 return and set errno to EINVAL*/
    return -EINVAL;
  }
  printk(KERN_DEBUG "current_msg == %p\n",current_msg);

  channel = current_msg->current_channel;
  
  /* no channel has been set to message*/
  if (channel == NULL)
  {
    printk(KERN_DEBUG "channel == NULL\n");
    /* The c library interprets this and gives -1 return and set errno to EINVAL*/
    return -EINVAL;
  }
  printk(KERN_DEBUG "channel == %p\n",channel);

  /* no message has been set on channel*/
  if (channel->msg == NULL)
  {
    printk(KERN_DEBUG "channel->msg == NULL\n");
    /* The c library interprets this and gives -1 return and set errno to EWOULDBLOCK*/
    return -EWOULDBLOCK;
  }
  printk(KERN_DEBUG "channel->msg == %p\n",channel->msg);

  /* buffer length is less then message length*/
  if (length < channel->msg_length)
  {
    printk(KERN_DEBUG "length < channel->msg_length\n");
    /* The c library interprets this and gives -1 return and set errno to ENOSPC*/
    return -ENOSPC;
  }

  content_msg = channel->msg;
  printk(KERN_DEBUG "channel->msg= %p\n",channel->msg);

  /* write current message to the buffer*/
  for (i = 0; i < channel->msg_length; i++)
  {
    /* put_user returns 0 on success*/
    if ((put_user(content_msg[i], &buffer[i])) != 0)
    {
      /* The c library interprets this and gives -1 return and set errno to EFAULT*/
      return -1;
    }
  }

  return channel->msg_length;
}

//==================== DEVICE SETUP =============================

// This structure will hold the functions to be called
// when a process does something to the device we created
struct file_operations Fops =
    {
        .owner = THIS_MODULE, // Required for correct count of module usage. This prevents the module from being removed while used.
        .read = device_read,
        .write = device_write,
        .open = device_open,
        .release = device_release,
        .unlocked_ioctl = device_ioctl,
};

// Initialize the module - Register the character device
static int __init simple_init(void)
{
  int rc = -1;

  printk(KERN_ALERT "in init\n");

  // Register driver capabilities. Obtain major num
  rc = register_chrdev( MAJOR_NUM, DEVICE_RANGE_NAME, &Fops );

  // Negative values signify an error
  if( rc < 0 )
  {
    printk( KERN_ALERT "%s registraion failed for  %d\n",
                       DEVICE_RANGE_NAME, MAJOR_NUM );
    return rc;
  }

  printk("Registeration is successful. "
         "The major device number is %d.\n",
         MAJOR_NUM);

  return SUCCESS;
}

//---------------------------------------------------------------
static void __exit simple_cleanup(void)
{
  int i;
  message *current_msg;
  message_channel *curr_channel;
  message_channel *next_channel;

  printk(KERN_ALERT "in exit\n");

  // Unregister the device
  // Should always succeed
  unregister_chrdev(MAJOR_NUM, DEVICE_RANGE_NAME);

  /* pass device_file_array and free used cells*/
  for (i = 0; i < DEVICE_FILE_BOUND; i++)
  {
    /* device_file_array[i] has been used*/
    if (device_file_array[i] != NULL)
    {
      current_msg = device_file_array[i];
      curr_channel = current_msg->first_channel;
      /* free all channel's struct*/
      while (curr_channel!=NULL)
      {
        next_channel=curr_channel-> next_message_channel;
        
        /* free content of message whice a memory was allocated for it*/
        if(curr_channel->msg!=NULL)
        {
          kfree(curr_channel->msg);
        }
        /* free channel*/
        kfree(curr_channel);
        curr_channel=next_channel;
      }
      kfree(current_msg);
    }
  }

  /*****************************************************************************/ 
  
  printk("exit module secseed");
}

//---------------------------------------------------------------
module_init(simple_init);
module_exit(simple_cleanup);
