#ifndef USB_GENERIC_H
#define USB_GENERIC_H

#define HANDSPRING_VENDOR_ID	0x082d
#define PALM_VENDOR_ID		0x0830

#define HS_USBFUN_GENERIC	0
#define HS_USBFUN_DEBUGGER	1
#define HS_USBFUN_HOTSYNC	2
#define HS_USBFUN_CONSOLE	3
#define HS_USBFUN_REMOTEFILESYS	4
#define HS_USBFUN_MAX		4

extern const char *HS_usb_functions[];

/*
 * Description of the Handspring Visor vendor specific USB commands.
 * [Is this truly only Handspring Visor stuff, or is it more universal? - mbd]
 */

/*
 * Queries for the number of bytes available to transmit to the host
 * for the specified endpoint.  Currently not used -- returns 0x0001.
 */
#define usbRequestVendorGetBytesAvailable		0x01

/*
 * This request is sent by the host to notify the device that the host
 * is closing a pipe.  An empty packet is sent in response.
 */
#define usbRequestVendorCloseNotification		0x02

/*
 * Sent by the host during enumeration to get the endpoints used by
 * the connection.  [This must be more than Visor since the m50x does
 * it too - mbd]
 */
#define usbRequestVendorGetConnectionInfo		0x03

/* Hooks to the OS specific interface functions */

/* probably need || defined(__NetBSD__) || defined(__OpenBSD__) */
#if defined(__FreeBSD__)
#include "usb_bsd.h"
#define USB_OPEN bsd_usb_open
#define USB_READ bsd_usb_read
#define USB_WRITE bsd_usb_write
#define USB_SELECT bsd_usb_select
#define USB_CLOSE bsd_usb_close
#elif defined(__linux__)
#include "usb_linux.h"
#define USB_OPEN linux_usb_open
#define USB_READ linux_usb_read
#define USB_WRITE linux_usb_write
#define USB_SELECT linux_usb_select
#define USB_CLOSE linux_usb_close
#else
#error "No USB OS hooks defined for your OS"
#endif

#endif /* USB_GENERIC_H */
