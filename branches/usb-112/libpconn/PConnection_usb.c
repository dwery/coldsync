/* PConnection_usb.c
 *
 * USB-related stuff for Visors.
 *
 *	Copyright (C) 2000, Louis A. Mamakos.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: PConnection_usb.c,v 1.2 2000-01-25 11:25:49 arensb Exp $
 */

#include "config.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>

#if WITH_USB		/* This encompasses the entire file */

#include <usb.h>

#if HAVE_LIBINTL
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL */

#include "pconn/PConnection.h"

struct usb_data {
	unsigned char iobuf[1024];	/* XXX - Is there anything
					 * magical about 1024? Make
					 * this a cpp symbol */
	unsigned char *iobufp;
	int iobuflen;
	int usb_ep0;
};

int usbdebug = 0;		/* XXX - Rename this usb_trace, and
				 * integrate it into the *_trace
				 * scheme.
				 */

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

	if (u->usb_ep0 >= 0) {
		(void) close(u->usb_ep0);
		u->usb_ep0 = -1;
	}
	return close(p->fd);
}

static int
usb_select(struct PConnection *p, int which, struct timeval *tvp) {
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
pconn_usb_open(struct PConnection *p, char *device)
{
	struct usb_data *u;
	struct usb_device_info udi;
	struct usb_ctl_request ur;
	char *usbep2;
	int i;
	unsigned char usbresponse[50];

	p->io_read = &usb_read;
	p->io_write = &usb_write;
	p->io_close = &usb_close;
	p->io_select = &usb_select;
	p->io_setspeed = &usb_setspeed;
	p->io_drain = &usb_drain;

	u = p->io_private = malloc(sizeof(struct usb_data));

	bzero(p->io_private, sizeof(struct usb_data));
	u->usb_ep0 = -1;

	for (i = 0; i < 30; i++) {
		if ((u->usb_ep0 = open(device, O_RDWR)) >= 0) 
			break;
		sleep(1);
	}

	(void) ioctl(u->usb_ep0, USB_SETDEBUG, &usbdebug);

	if (u->usb_ep0 < 0) {
		fprintf(stderr, _("%s: Can't open USB device"),
			"pconn_usb_open");
		perror("open");
		return -1;
	}

	if (ioctl(u->usb_ep0, USB_GET_DEVICEINFO, &udi)) {
		fprintf(stderr,
			_("%s: Can't get information about USB device"),
			"pconn_usb_open");
		perror("ioctl(USB_GET_DEVICEINFO)");
		exit(1);
	}

#define SURE(x) \
	(((x!=NULL) && (*x !='\0')) ? x : "<not defined>")

	/* XXX - Don't print this during normal operations. Put this in a
	 * USB_TRACE block.
	 */
	fprintf(stderr,
		"Device information: %s vendor %04x (%s) product %04x (%s) rev %s addr %x\n",
		device,  udi.vendorNo, SURE(udi.vendor), 
		udi.productNo, SURE(udi.product),
		SURE(udi.release), udi.addr);

	{
		int cfg = 1;

		if (ioctl(u->usb_ep0, USB_SET_CONFIG, &cfg) < 0) {
			perror("ioctl(USB_SET_CONFIG)");
			exit(1);
		}
		if (usbdebug)
			fprintf(stderr, "usb: set configuration %d\n", cfg);
	}


	/*-------------------------------------------------------------------
	 * Enter the twilight zone.
	 *
	 * Based on observing USB protocol analyzer traces, it appear that the
	 * windows hotsync process performs two vendor specific setup commands.
	 * I have no idea what they do, but this code reproduces those commands
	 * as well.
	 *
	 * Hopefully these are not specific to a particular model or software
	 * version or Visor serial number..
	 *-------------------------------------------------------------------*/

	bzero(&ur, sizeof(ur));
	ur.request.bmRequestType = UT_READ_VENDOR_ENDPOINT;
	ur.request.bRequest = 3;
	USETW(ur.request.wValue, 0);
	USETW(ur.request.wIndex, 0);
	USETW(ur.request.wLength, 18);
	ur.data = &usbresponse[0];
	ur.flags = USBD_SHORT_XFER_OK;
	ur.actlen = 0;

	if (ioctl(u->usb_ep0, USB_DO_REQUEST, &ur) < 0) {
	  perror(_("ioctl(USB_DO_REQUEST) 3 failed"));
	  exit(1);		/* XXX - This is rather extreme, isn't it? */
	}

	if (usbdebug > 1)
	  fprintf(stderr, "first setup 0x3 returns %d bytes\n", ur.actlen);

	bzero(&ur, sizeof(ur));
	ur.request.bmRequestType = UT_READ_VENDOR_ENDPOINT;
	ur.request.bRequest = 1;
	USETW(ur.request.wValue, 0);
	USETW(ur.request.wIndex, 5);
	USETW(ur.request.wLength, 2);
	ur.data = &usbresponse[0];
	ur.flags = USBD_SHORT_XFER_OK;
	ur.actlen = 0;

	if (ioctl(u->usb_ep0, USB_DO_REQUEST, &ur) < 0) {
		perror(_("ioctl(USB_DO_REQUEST) 1 failed"));
		exit(1);	/* XXX - This is rather extreme, isn't it? */
	}

	if (usbdebug > 1)
		fprintf(stderr, "first setup 0x1 returns %d bytes\n",
			ur.actlen);

	/* ------------------------------------------------------------------ 
	 * 
	 *  Ok, all of the device specific control messages have been
	 *  completed.  It is critically important that these be performed
	 *  before opening a full duplex conneciton to endpoint 2 on the
	 *  Visor, on which all of the data transfer occur.  If this is open
	 *  while the set configuration operation above is done, at best it
	 *  won't work, and at worst it will call a panic in your kernel..
	 *
	 * ---------------------------------------------------------------- */

	usbep2 = malloc(strlen(device)+20);
				/* XXX - Error-checking */
	sprintf(usbep2, "%s.2", device);

	p->fd = open(usbep2, O_RDWR, 0);

	if (p->fd < 0) {
		fprintf(stderr, _("%s: Can't open %s\n"),
			"pconn_usb_open", usbep2);
		perror("open");
		exit(1);	/* XXX - This is rather extreme, isn't it? */
	}
	if (usbdebug)
		fprintf(stderr, "usb: endpoint %s open for reading\n", usbep2);

	if ((i = fcntl(p->fd, F_GETFL, 0))!=-1) {
		i &= ~O_NONBLOCK;
		fcntl(p->fd, F_SETFL, i);
	}

	i = 1;
	if (ioctl(p->fd, USB_SET_SHORT_XFER, &i) < 0)
		perror("ioctl(USB_SET_SHORT_XFER)");
  
	free(usbep2);

	/* Make it so */
	return p->fd;
}

#endif	/* WITH_USB */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
