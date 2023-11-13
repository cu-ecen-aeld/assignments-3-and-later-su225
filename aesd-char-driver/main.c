/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include <linux/uaccess.h>

#include "aesdchar.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Suchith.J.N");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

/**
 * aesd_trim empties out the aesd device and deallocates all
 * the buffers associated with it. This must be called holding
 * the mutex 
 */
static void __aesd_trim_locked(struct aesd_dev *dev)
{
    PDEBUG("aesd_trim: reset aesd circular buffer");
    aesd_circular_buffer_destroy(&aesd_device.buf);
    aesd_circular_buffer_init(&aesd_device.buf);
}

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;
    PDEBUG("aesd_dev: open");
    
    /**
     * Set the private data part to point to our aesd device.
     * This allows us to identify AESD device specific info
     * attached to the file for subsequent operations.
     */
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;

    /**
     * We expect both O_TRUNC and O_APPEND flags to be present
     * while opening the file. Otherwise, we will reject it as
     * unsupported operation 
     */
    if ((filp->f_flags & O_APPEND) == 0 || (filp->f_flags & O_TRUNC) == 0)
        return -EOPNOTSUPP; 

    /**
     * If truncate flag is present, then make sure that all the
     * circular buffer entries are cleared before we open the file. 
     */
    if ((filp->f_flags & O_TRUNC) > 0) {
        /**
         * If we cannot grab the lock, then tell the userspace
         * to restart the syscall without blocking further. 
         */
        if (mutex_lock_interruptible(&aesd_device.lock))
            return -ERESTARTSYS;
        __aesd_trim_locked(dev);
        mutex_unlock(&aesd_device.lock);
    }

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("aesd_dev: release");
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    PDEBUG("aesd_dev: read %zu bytes with offset %lld",count,*f_pos);
    
    /* If the count is 0, then we don't have to read any data */
    if (count == 0)
        return 0;

    /* This should not happen. The file position is not specified */
    if (f_pos == NULL)
        return -EINVAL;

    loff_t pos = *f_pos;

    /**
     * We need to hold the lock throughout this operation so that the
     * contents would not be deallocated by concurrent writes while
     * we copy the data to the userspace. Even if we are interrupted,
     * we haven't yet updated any state and hence it is safe to retry.
     * Hence use the _interruptible() variant of the mutex. 
     */
    if (mutex_lock_interruptible(&aesd_device.lock))
        return -ERESTARTSYS;
    
    /**
     * Repeatedly read the bytes off the circular buffer and copy it to
     * the userspace memory. There are a few cases to handle
     * 1. There are enough bytes in the same segment => get the segment and done!
     * 2. The start and end are in different segments => need to update user buffer offset as well
     * 3. More bytes are asked than what is available => read everything from offset
     * 4. The offset is beyond the max => 0
     */
    size_t out_seg_offset;
    size_t bytes_read = 0, user_buff_offset = 0;
    while (bytes_read < count) {
        struct aesd_buffer_entry *entry = aesd_circular_buffer_find_entry_offset_for_fpos(
            &aesd_device.buf, pos, &out_seg_offset);
        
        /**
         * Entry would be NULL when we don't have data to be read. It
         * happens when the offset is beyond the maximum or when we are
         * asked too many bytes by the userspace. 
         */
        if (entry == NULL) {
            goto out;
        }

        ssize_t bytes_reqd = count - bytes_read;
        ssize_t bytes_available = entry->size - out_seg_offset;

        /* Calculate the bytes available for copying from the current entry */
        ssize_t copyable_bytes = bytes_reqd;
        if (bytes_available < bytes_reqd) {
            copyable_bytes = bytes_available;
        }

        /** 
         * Calculate the userspace and kernelspace addresses and copy the bytes 
         * into the userspace. This can be suspended by the kernel due to the
         * page fault. In other words, this can lead to the process calling it
         * to be suspended (if the user page supplied is paged out). This is one
         * reason why one cannot use spinlock, but must use a mutex.
         */
        void *userspace_offset = buf + user_buff_offset;
        void *kernelspace_offset = entry->buffptr + out_seg_offset;
        unsigned long copied = copy_to_user(userspace_offset, kernelspace_offset, 
            (unsigned long)copyable_bytes);
        
        user_buff_offset += copied;
        bytes_read += copied;
        pos += copied;
    }
out:
    /* Finally, update the position of the file for the next call */
    *f_pos = pos;
    mutex_unlock(&aesd_device.lock);
    return bytes_read;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("aesd_dev: write %zu bytes with offset %lld",count,*f_pos);
    
    char *cmd = strndup_user(buf, count);
    if (IS_ERR_OR_NULL(cmd))
        return PTR_ERR(cmd);

    /**
     * If the userspace sends a command which does not end with '\n'
     * then we will throw an error informing them to adhere with the
     * command format. It is much simpler to do this in userspace. 
     */
    __kernel_size_t cmd_size = strlen(cmd);
    if (cmd_size > 0 && cmd[cmd_size-1] != '\n') {
        PDEBUG("aesd_dev: command does not end with newline: %s", cmd);
        retval = -EINVAL;
        goto cleanup_cmd;
    }

    struct aesd_buffer_entry *entry;
    entry = kmalloc(sizeof(*entry), GFP_KERNEL);
    if (entry == NULL) {
        retval = -ENOMEM;
        goto cleanup_cmd;
    }
    entry->buffptr = cmd;
    entry->size = cmd_size;

    /**
     * We cannot use a spinlock here because there can be a free operation
     * internal to the ring buffer when replacing the oldest entry. This
     * means that there is a call to kfree() which can sleep. Hence, use mutex.
     * 
     * Here, mutex_lock() is used because the syscall is not restartable. In
     * other words, this is not idempotent and adding buffer entry is quick
     */
    mutex_lock(&aesd_device.lock);
    aesd_circular_buffer_add_entry(&aesd_device.buf, entry);
    mutex_unlock(&aesd_device.lock);

    kfree(entry);
    return cmd_size;
    
cleanup_cmd:
    /**
     * Internally, strndup_user calls a variant of kmalloc. Hence, we
     * have to cleanup the memory allocated for the string with strndup_user  
     */
    kfree(cmd);
    return retval;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add(&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}

int __init aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /**
     * Initialise the circular buffer needed for storing
     * entries from the userspace. This must be destroyed
     * by deallocating all the space.
     */
    aesd_circular_buffer_init(&aesd_device.buf);
    mutex_init(&aesd_device.lock);

    result = aesd_setup_cdev(&aesd_device);
    if(result) {
        unregister_chrdev_region(dev, 1);
    }
    return result;
}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * Make sure the memory associated with the
     * device is freed before destroying the mutex.
     * This is because it needs to be held which means
     * it needs to be alive.
     */
    mutex_lock(&aesd_device.lock);
    __aesd_trim_locked(&aesd_device);
    mutex_unlock(&aesd_device.lock);

    mutex_destroy(&aesd_device.lock);

    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
