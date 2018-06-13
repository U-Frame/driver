#include <linux/init.h>           // Macros used to mark up functions e.g. __init __exit
#include <linux/module.h>         // Core header for loading LKMs into the kernel
#include <linux/device.h>         // Header to support the kernel Driver Model
#include <linux/kernel.h>         // Contains types, macros, functions for the kernel
#include <linux/fs.h>             // Header for the Linux file system support
#include <linux/uaccess.h>          // Required for the copy to user function
#include <linux/cdev.h>
#include <linux/usb.h>
#include <linux/errno.h>

#include "uframe.h"
#include "uframechar.h"

 
MODULE_LICENSE("GPL");          
MODULE_AUTHOR("Mahmoud Nagy"); 
MODULE_DESCRIPTION("A generic kernel driver for UFrame"); 
MODULE_VERSION("0.5");     


int uframe_major;
int uframe_minor;

const int vid = 0x0000,pid=0x0538;
static struct usb_device_id usb_table [] =
{
    { USB_DEVICE(vid,pid) },
    { }
};

dev_t uframe_devno;
struct uframe_dev uframe_dev;

static int uframe_probe(struct usb_interface *intf, const struct usb_device_id *id);
static void uframe_disconnect(struct usb_interface *interface);
    
static void setup_cdev(struct cdev * ,int );

struct file_operations uframe_fops = 
{
    .owner = THIS_MODULE,
    .read = uframe_read,
    .write = uframe_write,
    .open = uframe_open,
    .release = uframe_release,
    .unlocked_ioctl = uframe_ioctl,
};

static struct usb_driver uframe_driver =
{
    .name = DEVICE_NAME,
    .id_table = usb_table,
    .probe = uframe_probe,
    .disconnect = uframe_disconnect,
};

void uframe_delete(struct kref *krf)
{	
    struct uframe_endpoint *ep = container_of(krf, struct uframe_endpoint, kref);
    kfree (ep->data); //free data allocated
}

static int __init uframe_init(void)
{
    int result;
    
    result = usb_register(&uframe_driver);
    if(result)
	pr_err("%s: Can't register driver, errno %d\n",DEVICE_NAME, result);
    result = alloc_chrdev_region(&uframe_devno,0,1,DEVICE_NAME);
    if(result < 0)
    {
	printk(KERN_WARNING "%s: can't allocate major number\n",DEVICE_NAME);
	return result;
    }
    uframe_major = MAJOR(uframe_devno);
    uframe_minor = MINOR(uframe_devno);

    uframe_dev.epcnt = 0; //initialize it to zero
    
    return 0;
}


static void __exit uframe_exit(void)
{  
    unregister_chrdev_region(uframe_devno,1);
    usb_deregister(&uframe_driver);
}

static int uframe_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
    struct usb_host_interface *iface_desc;
    struct usb_endpoint_descriptor *endpoint;
    int i;
    int retval;
    
    printk(KERN_INFO "%s: USB CONNECTED\n", DEVICE_NAME);

    iface_desc = intf->cur_altsetting;
    uframe_dev.epcnt = iface_desc->desc.bNumEndpoints + 1 ; // don't forget there is also the control ep
    uframe_dev.interface = intf;
    uframe_dev.udev = usb_get_dev(interface_to_usbdev(intf));
    uframe_dev.eps = kmalloc(sizeof(struct uframe_endpoint) * uframe_dev.epcnt, GFP_KERNEL);
    if(!uframe_dev.eps)
    {
	printk(KERN_ERR"%s: Could not allocate uframe_devs\n", DEVICE_NAME);
	retval = -ENOMEM;
	goto error;
    }
    memset(uframe_dev.eps,0,sizeof(struct uframe_endpoint) * uframe_dev.epcnt);

    uframe_dev.eps[0].type = TYPE_CONTROL;
    uframe_dev.eps[0].epaddr = 0;
    
    setup_cdev(&uframe_dev.eps[0].cdev,uframe_minor); // register the control endpoint
    // start from 1 since ep 0 is control
    for (i = 1; i < iface_desc->desc.bNumEndpoints+1; ++i)
    {
	endpoint = &iface_desc->endpoint[i-1].desc; // ep starts from 0 
	kref_init(&uframe_dev.eps[i].kref);
	uframe_dev.eps[i].epaddr = endpoint->bEndpointAddress; // set it for later
	if(endpoint->bEndpointAddress & USB_DIR_IN)
	{
	    uframe_dev.eps[i].dir = DIR_IN;
	    uframe_dev.eps[i].buffer_size = endpoint->wMaxPacketSize;
	    uframe_dev.eps[i].interval = endpoint->bInterval;
	    uframe_dev.eps[i].data = kmalloc(uframe_dev.eps[i].buffer_size,GFP_KERNEL);
	    if (!uframe_dev.eps[i].data)
	    {
		printk(KERN_ERR"%s: Could not allocate data attribute\n",DEVICE_NAME);
		retval = -ENOMEM;
		goto error;
	    }
	}
	else //out
	    uframe_dev.eps[i].dir = DIR_OUT;

	switch (endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
	{
	case USB_ENDPOINT_XFER_BULK:
	    uframe_dev.eps[i].type = TYPE_BULK;
	    break;
	case USB_ENDPOINT_XFER_CONTROL:
	    uframe_dev.eps[i].type = TYPE_CONTROL;
	    break;
	case USB_ENDPOINT_XFER_INT:
	    uframe_dev.eps[i].type = TYPE_INTERRUPT;
	    break;
	default:
	    printk(KERN_INFO"%s: This type not supported \n", DEVICE_NAME); 
	}
	printk(KERN_INFO "%s: Detected endpoint dir:%d type:%d\n",DEVICE_NAME,uframe_dev.eps[i].dir,uframe_dev.eps[i].type);
	setup_cdev(&uframe_dev.eps[i].cdev,uframe_minor +i); // setup corresponding cdev
    }
    
    return 0;

error:
    if (uframe_dev.eps)
	for(i =0; i < uframe_dev.epcnt ; i++)
	    kref_put(&uframe_dev.eps[i].kref, uframe_delete);
    kfree(uframe_dev.eps);
    usb_put_dev(uframe_dev.udev);
    return retval; 
}

static void uframe_disconnect(struct usb_interface *interface)
{
    int i;
    
    printk(KERN_INFO "%s: USB drive removed\n",DEVICE_NAME);

    if(uframe_dev.eps)
	for(i=0; i < uframe_dev.epcnt; i++)
	{
	    cdev_del(&uframe_dev.eps[i].cdev);
	    kref_put(&uframe_dev.eps[i].kref, uframe_delete);
	}
    
    usb_put_dev(uframe_dev.udev);
    kfree(uframe_dev.eps);
    uframe_dev.epcnt = 0;
}


static void setup_cdev(struct cdev *dev ,int minor)
{
    int err,devno = MKDEV(uframe_major,minor);
    cdev_init(dev,&uframe_fops);
    dev->ops = &uframe_fops;
    dev->owner = THIS_MODULE;
    err = cdev_add(dev,devno,1);
    if(err)
	printk(KERN_NOTICE "%s: Error %d adding node%d", DEVICE_NAME, err, minor);
}

module_init(uframe_init);
module_exit(uframe_exit);
