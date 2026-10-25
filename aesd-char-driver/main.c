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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
#include "aesd_ioctl.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Juan Barragan");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

size_t total_buffer_length(struct aesd_dev *dev)
{
    uint8_t index;
    size_t total_len = 0;
    struct aesd_buffer_entry *entry;
    AESD_CIRCULAR_BUFFER_FOREACH(entry,&dev->cbuf,index) 
    {
        total_len += entry->size;
    }
    return total_len;
}

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");

    //get the aesd_dev structure containing the passed inode's i_cdev.
    struct aesd_dev *dev = container_of(inode->i_cdev, struct aesd_dev, cdev);

    //set private_data
    filp->private_data = dev;

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");

    /**
     * No memory allocation occurred in aesd_open. Nothing to release.
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    size_t traversed = 0;
    size_t remaining = count;
    size_t unread_bytes = 0;
    size_t read_bytes = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);

    struct aesd_dev *dev = filp->private_data;
    if(mutex_lock_interruptible(&dev->lock))
    {
        return -ERESTARTSYS;
    }

    //nothing to return or f_pos is negative
    if(count == 0 || *f_pos < 0)
    {
        goto out;
    }

    //checking whether f_pos is at the EOF or beyond it
    if(*f_pos >= total_buffer_length(dev))
    {
        goto out;
    }

    while(remaining > 0)
    {
        size_t entry_offset_byte = 0;
        struct aesd_buffer_entry *match = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->cbuf,
                *f_pos, &entry_offset_byte);

        if(!match)
        {
            /**
             * If bytes have already been copied to the __user buf,
             * return partial read count.
            */
            if(0 < traversed)
            {
                retval = traversed;
            }
            goto out;
        }
        else
        {
            size_t bytes_to_copy = 0;
            /** 
             * Copy at most the remaining requested bytes or the bytes left in the entry.
             * Whichever is smaller.
            */
            if((match->size - entry_offset_byte) < remaining)
            {
                bytes_to_copy = (match->size - entry_offset_byte);
            }
            else
            {
                bytes_to_copy = remaining;
            }

            //copy_to_user returns how many bytes were not copied
            unread_bytes = copy_to_user((buf + traversed), (match->buffptr + entry_offset_byte), bytes_to_copy);

            //update our counters and file position
            read_bytes = bytes_to_copy - unread_bytes;
            remaining -= read_bytes;
            *f_pos += read_bytes;
            traversed += read_bytes;

            /**
             * If there were unread bytes leftover, return the amount that were read
             * and exit. Partial read or -EFAULT error.
             */
            if(unread_bytes)
            {
                if(0 < traversed)
                {
                    retval = traversed;
                }
                else
                {
                    retval = -EFAULT;
                }

                goto out;
            }
        }
    }

    retval = traversed;
    goto out;

    out:
        mutex_unlock(&dev->lock);
        return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    size_t total_written = 0;
    size_t traversed = 0;
    size_t remaining = count;
    char *end_of_packet = NULL;

    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);

    struct aesd_dev *dev = filp->private_data;
    if(mutex_lock_interruptible(&dev->lock))
    {
        return -ERESTARTSYS;
    }

    //nothing to write
    if(count == 0)
    {
        retval = 0;
        goto out;
    }

    
    char *kernel_copy = kmalloc(count, GFP_KERNEL);
    if(!kernel_copy)
    {
        goto out;
    }

    //kernel copy of the passed user buffer 
    if(copy_from_user(kernel_copy, buf, count))
    {
        kfree(kernel_copy);
        retval = -EFAULT;
        goto out;
    }

    char *curr_position = kernel_copy;

    //process until there's no more bytes to write
    while(remaining > 0)
    {
        //find '\n'
        end_of_packet = memchr(curr_position, '\n', remaining);
        
        if(end_of_packet == NULL) //'\n' wasn't found
        {
            traversed = remaining;
        }
        else
        {
            traversed = end_of_packet - curr_position + 1;
        } 

        //resizing working_entry to fit the new write data
        size_t tmp_size = dev->working_entry.size + traversed;
        char *tmp_buf = krealloc(dev->working_entry.buffptr, tmp_size, GFP_KERNEL);

        if(!tmp_buf)
        {
            /**
             * If krealloc failed after any bytes have been appended to working_entry,
             * return the number of bytes written during this call.
             */
            if(total_written > 0)
            {
                retval = (ssize_t)total_written;
            }
            kfree(kernel_copy);
            goto out;
        }

        //append the new bytes to the tmp_buffer
        memcpy(tmp_buf + dev->working_entry.size, curr_position, traversed);
        //update working_entry
        dev->working_entry.buffptr = tmp_buf;
        dev->working_entry.size = tmp_size;
        
        //update our counters and input position pointer
        curr_position += traversed;
        remaining -= traversed;
        total_written += traversed;

        /**
         * Entry is complete due to '\n' being found. 
         * Add completed entry to circular buffer.
         * Also, check to see if we had to overwrite an entry and free it.
        */
        if(end_of_packet)
        {
            const char *overwrite_value = aesd_circular_buffer_add_entry(&dev->cbuf, &dev->working_entry);
            
            if(overwrite_value)
            {
                kfree(overwrite_value);
            }

            dev->working_entry.buffptr = NULL;
            dev->working_entry.size = 0;
        }
        
    }

    kfree(kernel_copy);
    retval = (ssize_t)total_written;
    goto out;

    out:
        mutex_unlock(&dev->lock);
        return retval;
}

loff_t aesd_llseek(struct file *filp, loff_t offset, int whence)
{
    loff_t new_pos = 0;
    loff_t length = 0;
    struct aesd_dev *dev = filp->private_data;
    if(mutex_lock_interruptible(&dev->lock))
    {
        return -ERESTARTSYS;
    }

    length = total_buffer_length(dev);
    new_pos = fixed_size_llseek(filp, offset, whence, length);

    goto out;

    out:
        mutex_unlock(&dev->lock);
        return new_pos;
}

/**
 * Adjust the file offset (f_pos) parameter of @param filp based on the location specified by
 * @param write_cmd (the zero referenced command to locate) 
 * and @param write_cmd_offset (the zero referenced offset into the command)
 * @return 0 if successful, negative if error occurred:
 *  -ERESTARTSYS if mutex could not be obtained
 *  -EINVAL if write command or write_cmd_offset was out of range
 */
static long aesd_adjust_file_offset(struct file *filp, unsigned int write_cmd, unsigned int write_cmd_offset)
{
    long retval = -EINVAL;
    struct aesd_dev *dev = filp->private_data;

    if(mutex_lock_interruptible(&dev->lock))
    {
        return -ERESTARTSYS;
    }

    //check write_cmd isn't bigger than the possible amount of cmds in the circular buffer
    if(write_cmd >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
    {
        goto out;
    }
    
    uint8_t cmd_idx = dev->cbuf.out_offs;
    //if write_cmd is 0 we just need the offset of the current read ptr index
    if(write_cmd == 0)
    {
        //check entry is valid
        if(dev->cbuf.entry[cmd_idx].buffptr == NULL
            || dev->cbuf.entry[cmd_idx].size <= write_cmd_offset)
        {
            goto out;
        }

        //update f_pos with the offset
        filp->f_pos = write_cmd_offset;
    }
    else
    {
        size_t traversed_length = 0;
        while(write_cmd != 0)
        {
            //keep a running total of the length of the buffer that was traversed
            traversed_length += dev->cbuf.entry[cmd_idx].size;
            cmd_idx++;

            //circle the index back to the beginning of the buffer if we hit the end
            if(cmd_idx == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
            {
                cmd_idx = 0;
            }

            write_cmd--;
        }

        //check entry is valid
        if(dev->cbuf.entry[cmd_idx].buffptr == NULL
            || dev->cbuf.entry[cmd_idx].size <= write_cmd_offset)
        {
            goto out;
        }

        //update f_pos with the offset and the total length of the circular buffer that was traversed
        filp->f_pos = traversed_length + write_cmd_offset;
    }

    retval = 0;
    goto out;

    out:
        mutex_unlock(&dev->lock);
        return retval;

}

static long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    long retval;

    switch(cmd)
    {
        case AESDCHAR_IOCSEEKTO:
            struct aesd_seekto seekto;
            if(copy_from_user(&seekto, (const void __user *)arg, sizeof(seekto)) != 0)
            {
                retval = -EFAULT;
            }
            else
            {
                retval = aesd_adjust_file_offset(filp, seekto.write_cmd, seekto.write_cmd_offset);
            }
            break;
        default:
            break;
    }

    goto out;

    out:
        return retval;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
    .llseek =   aesd_llseek,
    .unlocked_ioctl = aesd_ioctl,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}

int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    mutex_init(&aesd_device.lock);
    aesd_circular_buffer_init(&aesd_device.cbuf);
    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    //free possible data in the working entry buffptr 
    kfree(aesd_device.working_entry.buffptr);

    //free the buffptrs in the circular buffer
    uint8_t index;
    struct aesd_buffer_entry *entry;
    AESD_CIRCULAR_BUFFER_FOREACH(entry,&aesd_device.cbuf,index) 
    {
       kfree(entry->buffptr);
    }
    unregister_chrdev_region(devno, 1);
}


module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
