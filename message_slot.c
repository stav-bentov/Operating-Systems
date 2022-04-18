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

struct chardev_info
{
  spinlock_t lock;
};

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
} message;

/*============ PARAMETERS ============*/

/* used to prevent concurent access into the same device */
static int dev_open_flag = 0;
static struct chardev_info device_info;

static int major; /* device major number */

/* "there can be at most 256 different message slots device files"
so we'll keep an array that will represent 256 device files, each of them
will point to his first channel*/
static message *device_file_array[DEVICE_FILE_BOUND];

//================== DEVICE FUNCTIONS ===========================
static int device_open(struct inode *inode,
                       struct file *file)
{
   /* for spinlock */
  int minor; /*device file minor number*/
  message *current_msg; /*device file minor number*/
  message_channel *channel; 

  /************************************************************************/
  unsigned long flags;
  printk("Invoking device_open(%p)\n", file);

  // We don't want to talk to two processes at the same time
  spin_lock_irqsave(&device_info.lock, flags);
  if (1 == dev_open_flag)
  {
    spin_unlock_irqrestore(&device_info.lock, flags);
    return -EBUSY;
  }
  ++dev_open_flag;
  spin_unlock_irqrestore(&device_info.lock, flags);
  /************************************************************************/

  minor = iminor(inode);
  printk(KERN_DEBUG "Minor (%d)\n", minor);

  /* there is no structure for the file being opened- so create one and sets pointer to it*/
  if (device_file_array[minor] == NULL)
  {
    current_msg = (message*)kmalloc(sizeof(message), GFP_KERNEL);
    channel = (message_channel*)kmalloc(sizeof(message_channel), GFP_KERNEL);
    /* failed kmalloc*/
    if (current_msg == NULL || channel == NULL)
    {
      return -1;
    }
    /*else, both allocations succeed- fill memory with zero*/
    memset(current_msg, 0, sizeof(*current_msg));
    memset(channel, 0, sizeof(*channel));

    current_msg->first_channel = channel;
    device_file_array[minor] = current_msg;
  }

  file->private_data = (void*)device_file_array[minor]; /* the file now has a pointer to the message slot file*/
  return SUCCESS;
}

//---------------------------------------------------------------
static int device_release(struct inode *inode,
                          struct file *file)
{
  unsigned long flags; // for spinlock
  printk("Invoking device_release(%p,%p)\n", inode, file);

  // ready for our next caller
  spin_lock_irqsave(&device_info.lock, flags);
  --dev_open_flag;
  spin_unlock_irqrestore(&device_info.lock, flags);
  return SUCCESS;
}

static long device_ioctl(struct file *file,
                         unsigned int ioctl_command_id,
                         unsigned long ioctl_param)
{
  message *current_msg; /* file's message*/
  message_channel *channel_pointer; /* pointer to a channel- we'll use it to search the channel according to channel id and create it if needed*/
  message_channel *new_channel;
  int channel_exist,first_exist=0;

  if (ioctl_command_id != MSG_SLOT_CHANNEL || ioctl_param <= 0)
  {
    /* The c library interprets this and gives -1 return and set errno to EINVAL*/
    return -EINVAL;
  }

  printk(KERN_DEBUG "Channel ID= %ld\n", ioctl_param);

  /* get the message of the file from private data-field where we saved a pointer
  to it and find the channel according to the given id or create one*/
  current_msg = (message*)file->private_data;

  if(current_msg==NULL)
  {
    printk(KERN_DEBUG "file descriptor isn't assosiated well (file->private_data is NULL)\n");
    return -1;
  }

  channel_pointer = (message_channel*)current_msg->first_channel;


  /*first- check if no first_channel has been set- (true- then create one and set first and current channel,
  false- then search for the channel if exist), then search for the channel- if exsit then update current_channel,
  else- create new channel, add to the end and update current_channel. */
  if(channel_pointer!=NULL)
  {
    printk(KERN_DEBUG "first exist\n");
    first_exist=1;
    if (channel_pointer->id == ioctl_param)
    {
      printk(KERN_DEBUG "first is the right one\n");
      /* first channel is the right one- else search for it or create it*/
      current_msg->current_channel = channel_pointer;
      channel_exist=1;
    }
    else
    {
      while (channel_pointer->next_message_channel != NULL)
      {
        channel_pointer = channel_pointer->next_message_channel;
        /* the channel exists*/
        if (channel_pointer->id == ioctl_param)
        {
          printk(KERN_DEBUG "first is not the right one\n");
          current_msg->current_channel = channel_pointer;
          channel_exist=1;
          break;
        }
      }
    }

    /* need to add the channel with id= ioctl_param*/
    if(!channel_exist)
    {
      printk(KERN_DEBUG "create new channel\n");
      new_channel = (message_channel*)kmalloc(sizeof(message_channel), GFP_KERNEL);
      /* failed kmalloc*/
      if (new_channel == NULL)
      {
        printk(KERN_DEBUG "malloc didnt secseed\n");
        return -1;
      }
      /*else, allocation succeed- fill memory with zero*/
      memset(new_channel, 0, sizeof(message_channel));

      /* add new channel and update current_channel*/
      if(!first_exist)
      { /*first channel isn't set so the new channel will be it*/
        channel_pointer=new_channel;
        printk(KERN_DEBUG "first channel added\n");
      }
      else
      { /*first channel is set- add the new channel with id=ioctl_param to the end of the "channel list"*/
        channel_pointer->next_message_channel = new_channel;
        printk(KERN_DEBUG "not first channel added\n");
      }
      current_msg->current_channel = new_channel;
    }
  }
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

  current_msg = (message*)file->private_data;

  /* no message has been set to file*/
  if (current_msg == NULL)
  {
    /* The c library interprets this and gives -1 return and set errno to EINVAL*/
    return -EINVAL;
  }

  channel = current_msg->current_channel;
  
  /* no channel has been set to message*/
  if (channel == NULL)
  {
    /* The c library interprets this and gives -1 return and set errno to EINVAL*/
    return -EINVAL;
  }

  /* the message's length is 0 or more then 128*/
  if (length == 0 || length > BUFFER_BOUND)
  {
    /* The c library interprets this and gives -1 return and set errno to EMSGSIZE*/
    return -EMSGSIZE;
  }

  /* allocate memory to the message content if needed*/
  if (channel->msg == NULL)
  {
    channel->msg = (char *)kmalloc(BUFFER_BOUND, GFP_KERNEL);
    /* failed kmalloc*/
    if (channel == NULL)
    {
      return -1;
    }
  }

  /* reset prievious message or set up the message in the channel*/
  memset(channel->msg, 0, sizeof(BUFFER_BOUND));
  content_msg = channel->msg;

  channel->msg_length = length;
  /* get the message from buffer and update channel content*/
  for (i = 0; i < length; ++i)
  {
    get_user(content_msg[i], &buffer[i]);
  }

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

  current_msg = (message*)file->private_data;
  
  /* no message has been set to file*/
  if (current_msg == NULL)
  {
    /* The c library interprets this and gives -1 return and set errno to EINVAL*/
    return -EINVAL;
  }

  channel = current_msg->current_channel;
  
  /* no channel has been set to message*/
  if (channel == NULL)
  {
    /* The c library interprets this and gives -1 return and set errno to EINVAL*/
    return -EINVAL;
  }

  /* no message has been set on channel*/
  if (channel->msg == NULL)
  {
    /* The c library interprets this and gives -1 return and set errno to EWOULDBLOCK*/
    return -EWOULDBLOCK;
  }

  /* buffer length is less then message length*/
  if (length < channel->msg_length)
  {
    /* The c library interprets this and gives -1 return and set errno to ENOSPC*/
    return -ENOSPC;
  }

  /* write current message to the buffer*/
  for (i = 0; i < channel->msg_length; ++i)
  {
    /* put_user returns 0 on success*/
    if ((put_user(content_msg[i], &buffer[i])) != 0)
    {
      /* The c library interprets this and gives -1 return and set errno to EFAULT*/
      return -ENOSPC;
    }
  }

  /* make sure it's in the right length!*/
  return length;
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
  // init dev struct
  memset( &device_info, 0, sizeof(struct chardev_info) );
  spin_lock_init( &device_info.lock );

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

  // Unregister the device
  // Should always succeed
  unregister_chrdev(major, DEVICE_RANGE_NAME);
}

//---------------------------------------------------------------
module_init(simple_init);
module_exit(simple_cleanup);
