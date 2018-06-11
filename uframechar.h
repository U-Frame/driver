#ifndef __UFRAME_CHAR__
#define __UFRAME_CHAR__

ssize_t uframe_write(struct file *, const char __user *, size_t , loff_t *);
ssize_t uframe_read(struct file *, char __user *, size_t , loff_t *);
int uframe_release(struct inode *node, struct file *filp);
int uframe_open(struct inode *node, struct file *filp);

#endif
