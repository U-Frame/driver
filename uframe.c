#include <linux/init.h>           // Macros used to mark up functions e.g. __init __exit
#include <linux/module.h>         // Core header for loading LKMs into the kernel
#include <linux/device.h>         // Header to support the kernel Driver Model
#include <linux/kernel.h>         // Contains types, macros, functions for the kernel
#include <linux/fs.h>             // Header for the Linux file system support
#include <linux/uaccess.h>          // Required for the copy to user function
#include <linux/cdev.h>

#define  DEVICE_NAME "uframe"
 
MODULE_LICENSE("GPLV2");          
MODULE_AUTHOR("Mahmoud Nagy"); 
MODULE_DESCRIPTION("A generic kernel driver for UFrame"); 
MODULE_VERSION("0.1");     

#define COMMAND_INDEX 0
#define BULK_INDEX 1
#define INTERRUPT_INDEX 2

#define ENDPOINT_CNT 3

int uframe_major;
int uframe_minor;

struct uframe_dev {
    int access_cnt;
    struct cdev cdev;
};

dev_t uframe_devno;
struct uframe_dev uframe_devs[ENDPOINT_CNT];

ssize_t uframe_write(struct file *, const char __user *, size_t , loff_t *);
ssize_t uframe_read(struct file *, char __user *, size_t , loff_t *);
int uframe_release(struct inode *node, struct file *filp);
int uframe_open(struct inode *node, struct file *filp);

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

static int __init uframe_init(void)
{
    int result;
    int i;
    result = alloc_chrdev_region(&uframe_devno,0,1,DEVICE_NAME);
    if(result < 0)
    {
	printk(KERN_WARNING "%s: can't allocate major number\n",DEVICE_NAME);
	return result;
    }
    uframe_major = MAJOR(uframe_devno);
    uframe_minor = MINOR(uframe_devno);
    for(i=0; i < ENDPOINT_CNT; i++)
	setup_cdev(&uframe_devs[i].cdev,uframe_minor +i);
    return 0;
}


static void __exit uframe_exit(void)
{
    int i;
    for(i=0; i < ENDPOINT_CNT; i++)
	cdev_del(&uframe_devs[i].cdev);
    unregister_chrdev_region(uframe_devno,1);
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
    dev->access_cnt++;
    return 0;
}


int uframe_release(struct inode *inode, struct file *filp)
{
    struct uframe_dev *dev;
    dev = container_of(inode->i_cdev, struct uframe_dev, cdev);
    dev->access_cnt--;
    return 0;
}
ssize_t uframe_read(struct file *filp, char __user *buff, size_t count, loff_t *offp)
{
    printk(KERN_INFO "%s: READ\n",DEVICE_NAME);
    return 0;
}
ssize_t uframe_write(struct file *filp, const char __user *buff, size_t count, loff_t *offp)
{
    printk(KERN_INFO "%s: WRITE\n",DEVICE_NAME);
    return count;
}


/*int uframe_ioctl (struct inode *node, struct file *filp, unsigned int, unsigned long )
{
}
*/

module_init(uframe_init);
module_exit(uframe_exit);
