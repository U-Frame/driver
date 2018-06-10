#include <linux/init.h>           // Macros used to mark up functions e.g. __init __exit
#include <linux/module.h>         // Core header for loading LKMs into the kernel
#include <linux/device.h>         // Header to support the kernel Driver Model
#include <linux/kernel.h>         // Contains types, macros, functions for the kernel
#include <linux/fs.h>             // Header for the Linux file system support
#include <linux/uaccess.h>          // Required for the copy to user function
#include <linux/cdev.h>
#include <linux/usb.h>
#include <linux/errno.h>

#define  DEVICE_NAME "uframe"
 
MODULE_LICENSE("GPL");          
MODULE_AUTHOR("Mahmoud Nagy"); 
MODULE_DESCRIPTION("A generic kernel driver for UFrame"); 
MODULE_VERSION("0.5");     

#define TYPE_CONTROL 0
#define TYPE_BULK 1
#define TYPE_INTERRUPT 2

#define DIR_IN 0
#define DIR_OUT 1

int uframe_major;
int uframe_minor;

const int vid = 0x1058,pid=0x0820;

struct uframe_dev {
    int type;
    int dir;
    int epno;
    int buffer_size; // if input determine the size
    void * data;
    struct kref kref; 
    struct cdev cdev;
};

static struct usb_device_id usb_table [] =
{
    { USB_DEVICE(vid,pid) },
    { }
};

dev_t uframe_devno;
struct uframe_dev *uframe_devs;
struct usb_device *uframe_udev;
struct usb_interface *uframe_interface ;
int epcnt = 0;

ssize_t uframe_write(struct file *, const char __user *, size_t , loff_t *);
ssize_t uframe_read(struct file *, char __user *, size_t , loff_t *);
int uframe_release(struct inode *node, struct file *filp);
int uframe_open(struct inode *node, struct file *filp);

static int uframe_probe(struct usb_interface *intf, const struct usb_device_id *id);
static void uframe_disconnect(struct usb_interface *interface);
    
static void setup_cdev(struct cdev * ,int );

struct file_operations uframe_fops = 
{
    .owner = THIS_MODULE,
    .read = uframe_read,
    .write = uframe_write,
    .open = uframe_open,
//    .ioctl = uframe_ioctl,
    .release = uframe_release,
};

static struct usb_driver uframe_driver =
{
    .name = DEVICE_NAME,
    .id_table = usb_table,
    .probe = uframe_probe,
    .disconnect = uframe_disconnect,
};

static void uframe_delete(struct kref *krf)
{	
    struct uframe_dev *dev = container_of(krf, struct uframe_dev, kref);
    kfree (dev->data); //free data allocated
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
    epcnt = iface_desc->desc.bNumEndpoints + 1 ; // don't forget there is also the control ep
    uframe_interface = intf;
    uframe_udev = usb_get_dev(interface_to_usbdev(intf));
    uframe_devs = kmalloc(sizeof(struct uframe_dev) * epcnt, GFP_KERNEL);
    if(!uframe_devs)
    {
	printk(KERN_ERR"%s: Could not allocate uframe_devs\n", DEVICE_NAME);
	retval = -ENOMEM;
	goto error;
    }
    memset(uframe_devs,0,sizeof(struct uframe_dev) * epcnt);

    uframe_devs[0].type = TYPE_CONTROL;
    uframe_devs[0].epno = 0;
    setup_cdev(&uframe_devs[0].cdev,uframe_minor); // register the control endpoint
    // start from 1 since ep 0 is control
    for (i = 1; i < iface_desc->desc.bNumEndpoints+1; ++i)
    {
	endpoint = &iface_desc->endpoint[i-1].desc; // ep starts from 0 
	kref_init(&uframe_devs[i].kref);
	uframe_devs[i].epno = i; // set it for later
	if(endpoint->bEndpointAddress & USB_DIR_IN)
	{
	    uframe_devs[i].dir = DIR_IN;
	    uframe_devs[i].buffer_size = endpoint->wMaxPacketSize;
	    uframe_devs[i].data = kmalloc(uframe_devs[i].buffer_size,GFP_KERNEL);
	    if (!uframe_devs[i].data)
	    {
		printk(KERN_ERR"%s: Could not allocate data attribute\n",DEVICE_NAME);
		retval = -ENOMEM;
		goto error;
	    }
	}
	else //out
	    uframe_devs[i].dir = DIR_OUT;

	switch (endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
	{
	case USB_ENDPOINT_XFER_BULK:
	    uframe_devs[i].type = TYPE_BULK;
	    break;
	case USB_ENDPOINT_XFER_CONTROL:
	    uframe_devs[i].type = TYPE_CONTROL;
	    break;
	case USB_ENDPOINT_XFER_INT:
	    uframe_devs[i].type = TYPE_INTERRUPT;
	    break;
	default:
	    printk(KERN_INFO"%s: This type not supported \n", DEVICE_NAME); 
	}
	printk(KERN_INFO "%s: Detected endpoint dir:%d type:%d\n",DEVICE_NAME,uframe_devs[i].dir,uframe_devs[i].type);
	setup_cdev(&uframe_devs[i].cdev,uframe_minor +i); // setup corresponding cdev
    }
    
    return 0;

error:
    for(i =0; i < epcnt ; i++)
	if (uframe_devs)
	    kref_put(&uframe_devs[i].kref, uframe_delete);
    kfree(uframe_devs);
    usb_put_dev(uframe_udev);
    return retval; 
}

static void uframe_disconnect(struct usb_interface *interface)
{
    int i;
    
    printk(KERN_INFO "%s: USB drive removed\n",DEVICE_NAME);
    
    for(i=0; i < epcnt; i++)
	cdev_del(&uframe_devs[i].cdev);
    
    if(uframe_devs)
	for(i =0; i < epcnt ; i++)
	    kref_put(&uframe_devs[i].kref, uframe_delete);
    
    usb_put_dev(uframe_udev);
    kfree(uframe_devs);
    epcnt = 0;
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



int uframe_open(struct inode *inode, struct file *filp)
{
    struct uframe_dev *dev;
    dev = container_of(inode->i_cdev, struct uframe_dev, cdev);
    filp->private_data = dev; // for other methods
    kref_get(&dev->kref);
    return 0;
}


int uframe_release(struct inode *inode, struct file *filp)
{
    struct uframe_dev *dev;
    dev = container_of(inode->i_cdev, struct uframe_dev, cdev);
    kref_put(&dev->kref,uframe_delete);
    return 0;
}

ssize_t uframe_read(struct file *filp, char __user *buff, size_t count, loff_t *offp)
{
    struct uframe_dev *dev;

    dev = filp->private_data;
    printk(KERN_INFO"%s: READ From endpoint %d, type %d, dir %d\n",DEVICE_NAME, dev->epno, dev->type, dev->dir);
    return 0;
}
ssize_t uframe_write(struct file *filp, const char __user *buff, size_t count, loff_t *offp)
{
    struct uframe_dev *dev;
    
    dev = filp->private_data;
    printk(KERN_INFO"%s: WRITE From endpoint %d, type %d, dir %d\n",DEVICE_NAME, dev->epno, dev->type, dev->dir);
    
    return count;
}


/*int uframe_ioctl (struct inode *node, struct file *filp, unsigned int, unsigned long )
{
}
*/

module_init(uframe_init);
module_exit(uframe_exit);
