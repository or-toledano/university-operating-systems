#undef __KERNEL__
#define __KERNEL__
#undef MODULE
#define MODULE

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>        /* For register_chrdev */
#include <linux/string.h>    /* For memset */
#include <linux/slab.h>      /* For slab allocation */
#include <linux/hashtable.h> /* For the hashtable */
#include <linux/list.h>      /* For the list */

MODULE_LICENSE("GPL");

/* For IOCTL operations */
#include "message_slot.h"

#define HASHTABLE_BITS 20
static DEFINE_HASHTABLE(channel_htable, HASHTABLE_BITS);

struct channel
{
        unsigned char slot_id;
        unsigned int channel_id;
        size_t message_length;
        char message[MSG_MAX_LEN];
        struct hlist_node channel_hnode;
};

/*
 * This should be randomly generated per module load for an adversary
 * resistant hash table using crypto modules.
 */

#define RANDOM_PEPPER 123456789

/*
 * A channel can be uniquely described as its slot_id
 * concatenated with its channel_id
 */
static long calc_key(unsigned char slot_id, unsigned int channel_id)
{
        long key = slot_id;
        key <<= sizeof(unsigned int);
        key |= channel_id;
        return key ^ RANDOM_PEPPER;
}

static long channel_key(struct channel *ch)
{
        return calc_key(ch->slot_id, ch->channel_id);
}

static struct channel *find_channel(unsigned char slot_id, unsigned int channel_id)
{
        struct channel *channel_cursor;
        long key = calc_key(slot_id, channel_id);
        hash_for_each_possible(channel_htable, channel_cursor, channel_hnode, key) if (channel_cursor->slot_id == slot_id && channel_cursor->channel_id == channel_id) return channel_cursor;
        return NULL;
}

static LIST_HEAD(slot_llist);

struct slot
{
        unsigned int minor;
        struct list_head slot_lhead;
};

static struct slot *find_slot(unsigned int minor)
{
        struct slot *slot_cursor;
        list_for_each_entry(slot_cursor, &slot_llist, slot_lhead) if (slot_cursor->minor == minor) return slot_cursor;
        return NULL;
}

static int new_slot(unsigned char minor)
{
        struct slot *slot;
        slot = kmalloc(sizeof(struct slot), GFP_KERNEL);
        if (slot == NULL)
                return -ENOMEM;
        slot->minor = minor;
        list_add(&slot->slot_lhead, &slot_llist);
        return SUCCESS;
}

static int device_open(struct inode *inode,
                       struct file *file)
{
        int new_status;
        unsigned char minor;
        if (inode == NULL || file == NULL)
                return -EINVAL;
        minor = (unsigned char)iminor(inode);
        if (find_slot(minor) == NULL)
        {
                new_status = new_slot(minor);
                if (new_status)
                        return new_status;
                file->private_data = (void *)0;
        }
        return SUCCESS;
}

static int device_release(struct inode *inode,
                          struct file *file)
{
        if (inode == NULL || file == NULL)
                return -EINVAL;
        return SUCCESS;
}

static ssize_t device_read(struct file *file,
                           char __user *buffer,
                           size_t length,
                           loff_t *offset)
{
        unsigned char minor;
        unsigned int channel_id;
        size_t message_length;
        ssize_t bytes_read;
        struct slot *slot;
        struct channel *channel;
        if (file == NULL || buffer == NULL || length > MSG_MAX_LEN)
                return -EINVAL;
        channel_id = (unsigned long)file->private_data;
        if (channel_id == 0)
                return -EINVAL;
        minor = (unsigned char)iminor(file_inode(file));
        slot = find_slot(minor);
        if (slot == NULL)
                return -EINVAL;
        channel = find_channel(minor, channel_id);
        if (channel == NULL)
                return -EWOULDBLOCK;
        message_length = channel->message_length;
        if (message_length == 0)
                return -EWOULDBLOCK;
        if (message_length > length)
                return -ENOSPC;
        bytes_read = simple_read_from_buffer(buffer, length, offset, channel->message, message_length);
        if (bytes_read >= 0 && (size_t)bytes_read != message_length)
                return -EIO;
        return bytes_read;
}

static ssize_t device_write(struct file *file,
                            const char __user *buffer,
                            size_t length,
                            loff_t *offset)
{
        unsigned char minor;
        unsigned int channel_id;
        ssize_t bytes_wrote;
        char buffer_for_atomic_write[MSG_MAX_LEN];
        struct slot *slot;
        struct channel *channel;
        if (file == NULL || buffer == NULL || length > MSG_MAX_LEN)
                return -EINVAL;
        channel_id = (unsigned long)file->private_data;
        if (channel_id == 0)
                return -EINVAL;
        minor = (unsigned char)iminor(file_inode(file));
        slot = find_slot(minor);
        if (slot == NULL)
                return -EINVAL;
        channel = find_channel(minor, channel_id);
        if (channel == NULL)
        { /* If this channel doesn't exist, then create it */
                channel = (struct channel *)kmalloc(sizeof(struct channel), GFP_KERNEL);
                if (channel == NULL)
                        return -ENOMEM;
                channel->slot_id = minor;
                channel->channel_id = channel_id;
                hash_add(channel_htable, &channel->channel_hnode, channel_key(channel));
        }
        bytes_wrote = simple_write_to_buffer(buffer_for_atomic_write, MSG_MAX_LEN, offset, buffer, length);
        if (bytes_wrote >= 0 && (size_t)bytes_wrote != length)
                return -EIO;
        strncpy(channel->message, buffer_for_atomic_write, length);
        channel->message_length = length;
        return bytes_wrote;
}

static long device_ioctl(struct file *file,
                         unsigned int ioctl_command_id,
                         unsigned long ioctl_param)
{
        unsigned int channel_id;
        unsigned char minor;
        if (file == NULL || ioctl_command_id != MSG_SLOT_CHANNEL || ioctl_param == 0)
                return -EINVAL;
        channel_id = (unsigned int)ioctl_param;
        minor = (unsigned char)iminor(file_inode(file));
        file->private_data = (void *)ioctl_param;
        return SUCCESS;
}

static struct file_operations Fops =
    {
        .read = device_read,
        .write = device_write,
        .open = device_open,
        .unlocked_ioctl = device_ioctl,
        .release = device_release,
        .owner = THIS_MODULE};

/*  Register the char device */
static int __init simple_init(void)
{
        int rc;
        rc = register_chrdev(MAJOR_NUM, CHAR_DEV_NAME, &Fops);
        if (rc < 0)
        {
                printk(KERN_ALERT "%s registraion failed for  %d\n",
                       DEVICE_FILE_NAME, MAJOR_NUM);
                return rc;
        }
        printk("Registeration successful.");
        printk("mknod /dev/%s c %d 0\n", DEVICE_FILE_NAME, MAJOR_NUM);
        return 0;
}

static void __exit simple_cleanup(void)
{
        int bkt;
        struct hlist_node *tmp;
        struct slot *slot_cursor;
        struct slot *slot_tmp;
        struct channel *channel_cursor;
        list_for_each_entry_safe(slot_cursor, slot_tmp, &slot_llist, slot_lhead)
            kfree(slot_cursor);
        hash_for_each_safe(channel_htable, bkt, tmp, channel_cursor, channel_hnode)
            kfree(channel_cursor);
        unregister_chrdev(MAJOR_NUM, CHAR_DEV_NAME);
}

module_init(simple_init);

module_exit(simple_cleanup);
