#ifndef __UFRAME_CHAR__
#define __UFRAME_CHAR__

#define IOCTL_INTERRUPT_INTERVAL 0
#define IOCTL_CONTROL_READ 1


ssize_t uframe_write(struct file *, const char __user *, size_t , loff_t *);
ssize_t uframe_read(struct file *, char __user *, size_t , loff_t *);
int uframe_release(struct inode *node, struct file *filp);
int uframe_open(struct inode *node, struct file *filp);
long uframe_ioctl(struct file *f, unsigned int cmd, unsigned long arg);

#endif
