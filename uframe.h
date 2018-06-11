#ifndef __UFRAME__
#define __UFRAME__

#define  DEVICE_NAME "uframe"

#define TYPE_CONTROL 0
#define TYPE_BULK 1
#define TYPE_INTERRUPT 2

#define DIR_IN 0
#define DIR_OUT 1

struct uframe_endpoint {
    int type;
    int dir;
    int epaddr;
    int timeout;
    int buffer_size; // if input determine the size
    char * data;
    struct kref kref; 
    struct cdev cdev;
};

struct uframe_dev {
    struct uframe_endpoint *eps;
    struct usb_device *udev;
    struct usb_interface *interface ;
    int epcnt;
};

extern struct uframe_dev uframe_dev;

void uframe_delete(struct kref *);

#endif