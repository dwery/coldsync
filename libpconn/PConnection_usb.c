/* PConnection_usb.c
 *
 * USB-related stuff for Visors.
 *
 *	Copyright (C) 2000, Louis A. Mamakos.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: PConnection_usb.c,v 1.8 2000-10-20 20:22:50 arensb Exp $
 */

#include "config.h"

#include <stdio.h>	/* This is left outside of the #if WITH_USB solely
			 * to make 'gcc' shut up: apparently, ANSI C
			 * doesn't like empty source files.
			 */

#if WITH_USB		/* This encompasses the entire file */
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>

#if STDC_HEADERS
# include <string.h>		/* For memcpy() et al. */
#else	/* STDC_HEADERS */
# ifndef HAVE_STRCHR
#  define strchr index
#  define strrchr rindex
# endif	/* HAVE_STRCHR */
# ifndef HAVE_MEMCPY
#  define memcpy(d,s,n)		bcopy ((s), (d), (n))
#  define memmove(d,s,n)	bcopy ((s), (d), (n))
# endif	/* HAVE_MEMCPY */
#endif	/* STDC_HEADERS */

#include <dev/usb/usb.h>

#if HAVE_LIBINTL_H
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL_H */

#include "pconn/PConnection.h"
#include "pconn/palm_types.h"

struct usb_data {
	unsigned char iobuf[1024];	/* XXX - Is there anything
					 * magical about 1024? Make
					 * this a cpp symbol */
	unsigned char *iobufp;
	int iobuflen;
};


/*
 *  Description of the Handspring Visor vendor specific USB
 *  commands.
 *
 ************************************************************************
 * USB Vendor Defined Request Codes  (bRequest)
 ************************************************************************
 * Queries for the number for bytes available to transmit to the host 
 * for the specified endpoint.  Currently not used -- returns 0x0001. 
 */
#define usbRequestVendorGetBytesAvailable		0x01

/* This request is sent by the host to notify the device that the host 
 * is closing a pipe.  An empty packet is sent in response.  
 */
#define usbRequestVendorCloseNotification		0x02 

/* Sent by the host during enumeration to get the endpoints used 
 * by the connection.
 */
#define usbRequestVendorGetConnectionInfo		0x03

/* note: uWord and uByte are defined in <dev/usb/usb.h> */
typedef struct {
	uWord		numPorts;
	struct {
		uByte	portFunctionID;
		uByte	port;
	} connections[20];
} UsbConnectionInfoType, * UsbConnectionInfoPtr;

#define	hs_usbfun_Generic	0
#define	hs_usbfun_Debugger	1
#define	hs_usbfun_Hotsync	2
#define	hs_usbfun_Console	3
#define	hs_usbfun_RemoteFileSys	4
#define	hs_usbfun_MAX		4

#define	HANDSPRING_VENDOR_ID	0x082d

static char *hs_usb_functions[] = {
	"Generic",
	"Debugger",
	"Hotsync",
	"Console",
	"RemoteFileSys",
	NULL
};


/*************************************************************************/



static int
usb_read(struct PConnection *p, unsigned char *buf, int len)
{
	/*
	 *  We've got to do intermediate buffering of the USB data
	 *  from the Visor's bulk endpoint.  This is because the kernel
	 *  USB driver isn't buffering any data, and we must have a read
	 *  operation pending at least as large as the USB transfer size
	 *  might potentially be.  
	 *
	 *  Here, we use a 1024 byte buffer, and return data out of
	 *  it.  This could return "short" reads to the caller, but
	 *  isn't semantically any different than reading an TTY device
	 *  which won't return a necessarily predicatable amount of
	 *  data.
	 */

	struct usb_data *u = p->io_private;
	int copy_len, retlen = 0;

	do {
		/* (?) Return leftover stuff from the previous read.
		 */
		if (u->iobuflen > 0) {
			copy_len = (len > u->iobuflen) ? u->iobuflen : len;

			bcopy(u->iobufp, buf, copy_len);
				/* XXX - Potential buffer overflow? */
				/* XXX - IIRC memcpy() is more "standard"
				 * than bcopy().
				 */
			u->iobufp += copy_len;
			u->iobuflen -= copy_len;
			buf += copy_len;
			len -= copy_len;
			retlen += copy_len;
		}

		if (retlen == 0) {
			/* (?) There wasn't anything left over from the
			 * last read(). Read some new stuff.
			 */
			if (u->iobuflen > 0) {
				fprintf(stderr,
					_("usb: trying to fill a non-empty "
					  "buffer\n"));
				abort();
			}

			u->iobufp = u->iobuf;
			u->iobuflen = read(p->fd, u->iobufp, sizeof(u->iobuf));
			if (u->iobuflen < 0) {
				perror("usb read");
				return u->iobuflen;
			}
		}
	} while (retlen == 0);

	return retlen;
}

static int
usb_write(struct PConnection *p, unsigned char *buf, int len)
{
	return write(p->fd, buf, len);
}

static int
usb_close(struct PConnection *p)
{	
	struct usb_data *u = p->io_private;

	free((void *)u);
	return close(p->fd);
}

static int
usb_select(struct PConnection *p, pconn_direction which,
	   struct timeval *tvp)
{
	fd_set fds;
	struct usb_data *u = p->io_private;

	FD_ZERO(&fds);
	FD_SET(p->fd, &fds);

	/*
	 *  If there's buffered read data, then return true.
	 */
	if ((which == forReading) && (u->iobuflen > 0)) {
		return 1;
	}

	/*
	 *  So this code looks really good, but in actuallity, the
	 *  ugen(4) kernel driver will always return ready.
	 *
	 *  Really fixing this would require something horrible, like
	 *  interrupting a read with an alarm signal.
	 */
	return (which == forReading) ? select(p->fd+1, &fds, NULL, NULL, tvp)
				     : select(p->fd+1, NULL, &fds, NULL, tvp);
}

static int
usb_setspeed(struct PConnection *p, int speed) {
	/*
	 * Nothing to do here..
	 */
	return 0;
}

static int
usb_drain(struct PConnection *p)
{
	return 0;
}

int
pconn_usb_open(struct PConnection *p, char *device, int prompt)
{
	struct usb_data *u;
	struct usb_device_info udi;
	struct usb_ctl_request ur;
	char *hotsync_ep_name;
	int usb_ep0 = -1, hotsync_endpoint = -1;
	int i;
	unsigned char usbresponse[50];
	UsbConnectionInfoType ci;

	p->io_read = &usb_read;
	p->io_write = &usb_write;
	p->io_close = &usb_close;
	p->io_select = &usb_select;
	p->io_setspeed = &usb_setspeed;
	p->io_drain = &usb_drain;

	u = p->io_private = malloc(sizeof(struct usb_data));

	bzero(p->io_private, sizeof(struct usb_data));

	/*
	 *  Prompt for the Hot Sync button now, as the USB bus
	 *  enumerator won't create the underlying device we want to
	 *  open until that happens.  The act of starting the hot sync
	 *  operation on the Visor logically plugs it into the USB
	 *  hub port, where it's noticed and enumerated.
	 */
	if (prompt)
		printf(_("Please press the HotSync button.\n"));

	/*
	 *  We've got to loop trying to open the USB device since
	 *  you'll get an ENXIO until the device has been inserted
	 *  on the USB bus.
	 */

	for (i = 0; i < 30; i++) {
		if ((usb_ep0 = open(device, O_RDWR | O_BINARY)) >= 0) 
				/* The O_BINARY flag is rather bogus, since
				 * the only relevant platform that uses it
				 * is Windows, and this USB code only works
				 * under FreeBSD. But hey, it doesn't cost
				 * anything, and it makes things
				 * consistent.
				 */
			break;

		IO_TRACE(1)
			perror(device);

		if (errno != ENXIO) {
			perror("open");
			/*  If some other error, don't bother waiting
			 *  for the timeout to expire.
			 */
			break;
		}
		sleep(1);
	}

	/*
	 *  If we've enabled trace for I/O, then poke the USB kernel
	 *  driver to turn on the minimal amount of tracing.  This can
	 *  fail if the kernel wasn't built with UGEN_DEBUG defined, so
	 *  we just ignore any error which might occur.
	 */
	IO_TRACE(1)
		i = 1;
	else
		i = 0;
	(void) ioctl(usb_ep0, USB_SETDEBUG, &i);


	/*
	 *  Open the control endpoint of the USB device.  We'll use this
	 *  to figure out if the device in question is the one we are
	 *  interested in and understand, and then to configure it in
	 *  preparation of doing I/O for the actual hot sync operation.
	 */
	if (usb_ep0 < 0) {
		fprintf(stderr, _("%s: Can't open USB device"),
			"pconn_usb_open");
		perror("open");
		return -1;
	}

	if (ioctl(usb_ep0, USB_GET_DEVICEINFO, &udi)) {
		fprintf(stderr,
			_("%s: Can't get information about USB device"),
			"pconn_usb_open");
		perror("ioctl(USB_GET_DEVICEINFO)");
		(void) close(usb_ep0);
		free((void *)u);
		return -1;
	}

#define SURE(x) \
	(((x!=NULL) && (*x !='\0')) ? x : "<not defined>")

	/*
	 *  Happily, all of the multibyte values in the struct usb_device_info
	 *  are in host byte order, and don't need to be converted.
	 */
	IO_TRACE(1) {
		fprintf(stderr,
  "Device information: %s vendor %04x (%s) product %04x (%s) rev %s addr %x\n",
			device,  udi.vendorNo, SURE(udi.vendor), 
			udi.productNo, SURE(udi.product),
			SURE(udi.release), udi.addr);

	}

	if (udi.vendorNo != HANDSPRING_VENDOR_ID) {
		fprintf(stderr,
			_("%s: Warning: Unexpected USB vendor ID 0x%x\n"),
			"pconn_usb_open", udi.vendorNo);
	}

	/*
	 *  Eventually, it might be necessary to split out the following
	 *  code should another Palm device with a USB peripheral interface
	 *  need to be supported in a different way.  Hopefully, they will
	 *  simply choose to inherit this existing interface rather then
	 *  inventing Yet Another exquisitely round wheel of their own.
	 */


	/*
	 *  Ensure that the device is set to the default configuration.
	 *  For Visors seen so far, the default is the only one they
	 *  support.
	 */
	i = 1;
	if (ioctl(usb_ep0, USB_SET_CONFIG, &i) < 0) {
		perror("warning: ioctl(USB_SET_CONFIG) failed");
	}

	/*
	 *  Now, ask the device (which we believe to be a Handspring Visor)
	 *  about the various USB endpoints which we can make a connection 
	 *  to.  Obviously, here we're looking for the endpoint associated 
	 *  with the Hotsync running on the other end.  This has been observed
	 *  to be endpoint "2", but this is not statically defined, and might
	 *  change based on what other applications are running and perhaps
	 *  on future hardware platforms.
	 */
	bzero(&ur, sizeof(ur));
	ur.request.bmRequestType = UT_READ_VENDOR_ENDPOINT;
	ur.request.bRequest = usbRequestVendorGetConnectionInfo;
	USETW(ur.request.wValue, 0);
	USETW(ur.request.wIndex, 0);
	USETW(ur.request.wLength, 18);
	ur.data = (void *) &ci;
	ur.flags = USBD_SHORT_XFER_OK;
	ur.actlen = 0;
	bzero((char *)&ci, sizeof(ci));
	if (ioctl(usb_ep0, USB_DO_REQUEST, &ur) < 0) {
		perror(_("ioctl(USB_DO_REQUEST) usbRequestVendorGetConnectionInfo failed"));
		(void) close(usb_ep0);
		free((void *)u);
		return -1;	  
	}


	/*
	 *  Now search the list of functions supported over the USB interface
	 *  for the endpoint associated with the HotSync function.  So far,
	 *  this has seen to "always" be on endpoint 2, but this might 
	 *  change and we use this binding mechanism to discover where it
	 *  lives now.  Sort of like the portmap(8) daemon..
	 *
	 *  Also, beware:  as this is "raw" USB function, the result
	 *  we get is in USB-specific byte order.  This happens to be
	 *  little endian, but we should use the accessor macros to ensure
	 *  the code continues to run on big endian CPUs too.
	 */
	for (i = 0; i < UGETW(ci.numPorts); i++) {
	  IO_TRACE(2)
	  	fprintf(stderr,
			"ConnectionInfo: entry %d function %s on port %d\n",
			i, 
			(ci.connections[i].portFunctionID <= hs_usbfun_MAX)
			  ? hs_usb_functions[ci.connections[i].portFunctionID]
			  : "unknown",
			ci.connections[i].port);

	  if (ci.connections[i].portFunctionID == hs_usbfun_Hotsync)
	  	hotsync_endpoint = ci.connections[i].port;
	}

	if (hotsync_endpoint < 0) {
		fprintf(stderr,
			_("%s: Could not find HotSync endpoint on Visor.\n"),
			"PConnection_usb");
		(void) close(usb_ep0);
		free((void *)u);
		return -1;	  
	}

	bzero(&ur, sizeof(ur));
	ur.request.bmRequestType = UT_READ_VENDOR_ENDPOINT;
	ur.request.bRequest = usbRequestVendorGetBytesAvailable;
	USETW(ur.request.wValue, 0);
	USETW(ur.request.wIndex, 5);
	USETW(ur.request.wLength, 2);
	ur.data = &usbresponse[0];
	ur.flags = USBD_SHORT_XFER_OK;
	ur.actlen = 0;

	if (ioctl(usb_ep0, USB_DO_REQUEST, &ur) < 0) {
		perror(_("ioctl(USB_DO_REQUEST) usbRequestVendorGetBytesAvailable failed"));
	}

	IO_TRACE(2) {
		fprintf(stderr, "first setup 0x1 returns %d bytes: ",
			ur.actlen);
		for (i = 0; i < ur.actlen; i++) {
		  fprintf(stderr, " 0x%02x", usbresponse[i]);
		}
		fprintf(stderr, "\n");
	}

	if (UGETW(usbresponse) != 1) {
		fprintf(stderr,
			_("%s: unexpected response %d to GetBytesAvailable\n"),
			"PConnection_usb", UGETW(usbresponse));
	}


	(void) close(usb_ep0);

	/* ------------------------------------------------------------------ 
	 * 
	 *  Ok, all of the device specific control messages have been
	 *  completed.  It is critically important that these be performed
	 *  before opening a full duplex conneciton to the hot sync
	 *  endpoint on the Visor, on which all of the data transfer occur.
	 *  If this is open while the set configuration operation above is
	 *  done, at best it won't work (in FreeBSD 4.0 and later), and at
	 *  worst it will cause a panic in your kernel.. (pre FreeBSD 4.0)
	 *
	 * ---------------------------------------------------------------- */


	/*
	 *  Construct the name of the device corresponding to the
	 *  USB endpoint associated with the hot sync service.
	 */
	if ((hotsync_ep_name = malloc(strlen(device)+20)) == NULL) {
		free((void *)u);
		return -1;
	}

	sprintf(hotsync_ep_name, "%s.%d", device, hotsync_endpoint);

	p->fd = open(hotsync_ep_name, O_RDWR, 0);

	if (p->fd < 0) {
		fprintf(stderr, _("%s: Can't open %s\n"),
			"pconn_usb_open", hotsync_ep_name);
		perror("open");
		(void) close(usb_ep0);
		free(hotsync_ep_name);
		free((void *)u);
		return -1;	  
	}

	if ((i = fcntl(p->fd, F_GETFL, 0))!=-1) {
		i &= ~O_NONBLOCK;
		fcntl(p->fd, F_SETFL, i);
	}

	i = 1;
	if (ioctl(p->fd, USB_SET_SHORT_XFER, &i) < 0)
		perror("ioctl(USB_SET_SHORT_XFER)");
  
	free(hotsync_ep_name);

	/* Make it so */
	return p->fd;
}

#endif	/* WITH_USB */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
