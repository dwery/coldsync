#ifndef USB_BSD_H
#define USB_BSD_H

int bsd_usb_open(const char *device, int prompt, void **private);
int bsd_usb_read(int fd, unsigned char *buf, int len, void *private);
int bsd_usb_write(int fd, unsigned const char *buf, const int len,
    void *private);
int bsd_usb_select(int fd, int for_reading, struct timeval *tvp,
    void *private);
int bsd_usb_close(int fd, void *private);

#endif /* USB_BSD_H */
