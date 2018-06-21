#include <linux/kernel.h>         // Contains types, macros, functions for the kernel
#include <linux/fs.h>             // Header for the Linux file system support
#include <linux/uaccess.h>          // Required for the copy to user function
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/usb.h>
#include <linux/types.h>

#include "uframe.h"
#include "uframechar.h"

struct control_params {
    uint8_t request;
    uint8_t request_type;
    uint16_t value;
    uint16_t index;
    uint16_t size;
};


int uframe_open(struct inode *inode, struct file *filp)
{
    struct uframe_endpoint *ep;
    ep = container_of(inode->i_cdev, struct uframe_endpoint, cdev);
	
    if(ep->type != TYPE_CONTROL && ep->dir == DIR_OUT && (filp->f_flags & O_ACCMODE) != O_WRONLY) 
	return -EACCES;
    else if(ep->type != TYPE_CONTROL && ep->dir == DIR_IN && (filp->f_flags & O_ACCMODE) != O_RDONLY)
	return -EACCES;
    
    filp->private_data = ep; // for other methods
    kref_get(&ep->kref);
    return 0;
}


int uframe_release(struct inode *inode, struct file *filp)
{
    struct uframe_endpoint *ep;
    ep = container_of(inode->i_cdev, struct uframe_endpoint, cdev);
    kref_put(&ep->kref,uframe_delete);
    return 0;
}

ssize_t uframe_read(struct file *filp, char __user *buff, size_t count, loff_t *offp)
{
    struct uframe_endpoint *ep;
    int retval = 0;
    ep = filp->private_data;
    
    printk(KERN_INFO"%s: READ From endpoint %d, type %d, dir %d\n",DEVICE_NAME,ep->epaddr, ep->type, ep->dir);

    if(!count)
	return 0;
    
    switch(ep->type)
    {
    case TYPE_CONTROL:
	break;
    case TYPE_BULK:
	retval = usb_bulk_msg(uframe_dev.udev,
			      usb_rcvbulkpipe(uframe_dev.udev, ep->epaddr),
			      ep->data,
			      min(ep->buffer_size, (int) count),
			      (int *) &count, HZ*10);
	break;
    case TYPE_INTERRUPT:
	retval = usb_interrupt_msg(uframe_dev.udev,
			     usb_rcvintpipe(uframe_dev.udev, ep->epaddr),
				   ep->data,
				   min(ep->buffer_size,(int) count),
				   (int *) &count, HZ*10);
	break;
    }

    printk(KERN_INFO "%s: read Data %s\n",DEVICE_NAME,ep->data);
    /* if the read was successful, copy the data to userspace */
    if (!retval)
    {
	printk(KERN_INFO "%s: Copying buffer to user\n",DEVICE_NAME);
	if (copy_to_user(buff, ep->data, min(ep->buffer_size,(int) count)))
	{
	    printk(KERN_ERR"%s: Error Copying data to user\n",DEVICE_NAME);
	    retval = -EFAULT;
	}
	else
	    retval = min(ep->buffer_size,(int) count);
    }
    return retval;

}


ssize_t uframe_write(struct file *filp, const char __user *buff, size_t count, loff_t *offp)
{
    struct uframe_endpoint *ep;
    int retval;
    char *buf;
    struct control_params *cparams;
    buf = kmalloc(count,GFP_KERNEL);
    retval = 0;
    ep = filp->private_data;    
    printk(KERN_INFO"%s: WRITE From endpoint %d, type %d, dir %d\n",DEVICE_NAME,ep->epaddr, ep->type, ep->dir);

    if (!count)
	return 0;
    
    if (copy_from_user(buf, buff, count))
	return -EFAULT;
     	
    switch(ep->type)
    {
    case TYPE_CONTROL:
	printk(KERN_INFO "%s: control type\n", DEVICE_NAME);
	cparams = (struct control_params *) buf;
	buf = buf + sizeof(struct control_params);
	printk(KERN_INFO "%s: request %d request_type %d value %d index %d size %d data %s\n",DEVICE_NAME,
	       cparams->request, cparams->request_type, cparams->value, cparams->index,cparams->size, buf);
   
	retval = usb_control_msg(uframe_dev.udev, usb_sndctrlpipe(uframe_dev.udev,ep->epaddr),
				 cparams->request, cparams->request_type, cparams->value, cparams->index,
				 buf,cparams->size, HZ *10);	    

	break;
    case TYPE_BULK:
	retval = usb_bulk_msg(uframe_dev.udev,
			      usb_sndbulkpipe(uframe_dev.udev, ep->epaddr),
			      buf,
			      (int) count,
			      (int *) &count, HZ*10);
	break;
    case TYPE_INTERRUPT:
	retval = usb_interrupt_msg(uframe_dev.udev,
				   usb_sndintpipe(uframe_dev.udev, ep->epaddr),
				   buf,
				   (int) count,
				   (int *) &count, HZ*10);
	break;
    }
    if(!retval)
	retval = count;
    return retval;
}

struct __ep_desc {
    int type; 
    int dir;
    int epaddr;
    int interval;
    int buffer_size; 
};

long uframe_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct uframe_endpoint *ep;
    int retval;
    struct control_params *cparams = NULL;
    char *data;
    struct __ep_desc _ep_desc;
    
    cparams = kmalloc(sizeof(struct control_params),GFP_KERNEL);
    retval = 0;
    ep = filp->private_data;    
    printk(KERN_INFO"%s: IOCTL From endpoint %d, type %d, dir %d\n",DEVICE_NAME,ep->epaddr, ep->type, ep->dir);

    switch(cmd)
    {
    case IOCTL_INTERRUPT_INTERVAL:
	if(ep->type == TYPE_INTERRUPT)
	{
	    if(copy_to_user((int __user *) arg, &ep->interval, sizeof(int)))
	       return -EFAULT;

	 }else
	      return -ENOTTY;
	    break;

    case IOCTL_CONTROL_READ:
	if(copy_from_user(cparams, (struct control_params __user*) arg, sizeof(struct control_params)))
	    return -EFAULT;

	data = kmalloc(cparams->size,GFP_KERNEL);
	retval = usb_control_msg(uframe_dev.udev, usb_rcvctrlpipe(uframe_dev.udev,ep->epaddr),
				 cparams->request, cparams->request_type, cparams->value, cparams->index,
				 data,cparams->size, HZ *10);
	if(retval < 0)
	{
	    printk(KERN_ERR"%s: couldn't control message to read\n",DEVICE_NAME);
	    return retval;
	}
	if(cparams->size)
	{
	    if(copy_to_user((char __user*) (arg + sizeof(struct control_params)), data, cparams->size))
	       return -EFAULT;
	}
	
	break;

    case IOCTL_ENDPOINTS_DESC:
        _ep_desc.type = ep->type;
	_ep_desc.interval = ep->interval;
	_ep_desc.dir = ep->dir;
	_ep_desc.epaddr = ep->epaddr;
	_ep_desc.buffer_size = ep->buffer_size;
	printk(KERN_INFO"%s: endpoints type %d interval %d dir %d epaddr %d size %d \n",DEVICE_NAME, _ep_desc.type,
	       _ep_desc.interval, _ep_desc.dir, _ep_desc.epaddr, _ep_desc.buffer_size);
	if(copy_to_user((struct __ep_desc  __user *) arg, &_ep_desc, sizeof(struct __ep_desc))) 
	    return -EFAULT;
       
	retval = 0;
	break;
    default: // not defined
	return -ENOTTY;
    }
	return retval;
}
