/* PConnection_usb.c
 *
 * USB-related stuff for Visors.
 *
 *	Copyright (C) 2000, Louis A. Mamakos.
 *	Copyright (C) 2002, Alessandro Zummo.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: PConnection_libusb.c,v 1.1 2002-11-23 16:32:40 azummo Exp $
 */

#include "config.h"

#include <stdio.h>	/* This is left outside of the #if WITH_USB solely
			 * to make 'gcc' shut up: apparently, ANSI C
			 * doesn't like empty source files.
			 */

#if WITH_LIBUSB		/* This encompasses the entire file */
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

#include <usb.h>

#if HAVE_LIBINTL_H
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL_H */

#include "pconn/PConnection.h"
#include "palm.h"
#include "pconn/palm_errno.h"
#include "pconn/netsync.h"

/* In 'struct usb_device_info' in FreeBSD 4.5 (and later?), the fields
 * begin with "udi_". In earlier version, they don't. Thus, under FreeBSD
 * 4.4, we use to 'udi.vendorNo'; under 4.5, we need to use
 * 'udi.udi_vendorNo'.
 *
 * Likewise in 'struct usb_ctl_request', the fields now begin with "ucr_".
 *
 * XXX - This relies on the fact that the only difference is a prefix in
 * the fields' names. If the situation ever becomes more complex (e.g.,
 * supporting USB under Solaris), it might become necessary to write a set
 * of inline wrapper functions.
 * XXX - Peter Haight supplied a patch for this.
 */
#if WITH_UDI_PREFIX
#  define UDI(field)	udi_##field
#  define UCR(field)	ucr_##field
#else	/* WITH_UDI_PREFIX */
#  define UDI(field)	field
#  define UCR(field)	field
#endif	/* WITH_UDI_PREFIX */

#define IOBUF_LEN	1024

struct usb_data {
	usb_dev_handle *dev;
	int hotsync_ep;
	unsigned char iobuf[IOBUF_LEN];
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

typedef struct {
	unsigned short	numPorts;
	struct {
		unsigned char	portFunctionID;
		unsigned char	port;
	} connections[20];
} UsbConnectionInfoType, * UsbConnectionInfoPtr;

#define	hs_usbfun_Generic	0
#define	hs_usbfun_Debugger	1
#define	hs_usbfun_Hotsync	2
#define	hs_usbfun_Console	3
#define	hs_usbfun_RemoteFileSys	4
#define	hs_usbfun_MAX		4

/* Expected device vendor IDs.
 * Device		Vendor	Product	Revision
 * Handspring Visor	0x82d
 * Palm M505		0x830	0x0002	0x0100
 * Sony Clie		0x054c
 */
#define	HANDSPRING_VENDOR_ID	0x082d
#define PALM_VENDOR_ID		0x0830
#define SONY_VENDOR_ID		0x054c

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
usb_bind(PConnection *pconn,
	 const void *addr,
	 const int addrlen)
{
	switch (pconn->protocol)
	{
	    case PCONN_STACK_FULL:
		return slp_bind(pconn, (const struct slp_addr *) addr);

	    case PCONN_STACK_SIMPLE:	/* Fall through */
	    case PCONN_STACK_NET:
		return 0;

	    case PCONN_STACK_NONE:
	    case PCONN_STACK_DEFAULT:
		/* XXX - Error */
	    default:
		/* XXX - Indicate error: unsupported protocol */
		return -1;
	}
}

static int
usb_read(PConnection *p, unsigned char *buf, int len)
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

			memcpy(buf, u->iobufp, copy_len);
				/* XXX - Potential buffer overflow? */
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
					"buffer.\n"));
				abort();
			}

			u->iobufp = u->iobuf;
			IO_TRACE(2)
				fprintf(stderr, "calling usb_bulk_read(%02x)\n", u->hotsync_ep | 0x80);
			u->iobuflen = usb_bulk_read(u->dev, u->hotsync_ep | 0x80,
				(char *)u->iobufp, sizeof(u->iobuf), 5000);
			IO_TRACE(2)
				fprintf(stderr, "usb read %d, ret %d\n", sizeof(u->iobuf), u->iobuflen);
			if (u->iobuflen < 0) {
				fprintf(stderr, "usb read: %s\n", usb_strerror());
				return u->iobuflen;
			}
		}
	} while (retlen == 0);

	return retlen;
}

static int
usb_write(PConnection *p, unsigned const char *buf, const int len)
{
	struct usb_data *u = p->io_private;
	int ret;

	ret = usb_bulk_write(u->dev, u->hotsync_ep, (char *)buf, len, 5000);
	IO_TRACE(2)
		fprintf(stderr, "usb write %d, ret %d\n", len, ret);
	if (ret < 0)
		fprintf(stderr, "usb write: %s\n", usb_strerror());
	return ret;
}

static int
usb_connect(PConnection *p, const void *addr, const int addrlen)
{
	return -1;		/* Not applicable to USB connection */
}

static int
usb_accept(PConnection *pconn)
{
	int err;
	udword newspeed;		/* Not really a speed; this is just
					 * used for the return value from
					 * cmp_accept().
					 */

printf("usb_accept: %d\n", pconn->protocol);
	switch (pconn->protocol)
	{
	    case PCONN_STACK_FULL:
		/* Negotiate a CMP connection. Since this is USB, the speed
		 * is meaningless: it'll just go however fast it can.
		 */
		newspeed = cmp_accept(pconn, 0);
		if (newspeed == ~0)
			return -1;
		break;

	    case PCONN_STACK_SIMPLE:	/* Fall through */
	    case PCONN_STACK_NET:
		/* XXX - The m500 hangs right around here. The sync hangs
		 * until the Palm aborts. This suggests that the Palm is
		 * waiting for data from the desktop. Or maybe the Palm
		 * wants to be tickled in some non-obvious way.
		 */
		IO_TRACE(5)
			fprintf(stderr, "usb_accept simple/net\n");
/* XXX - It seems to help here to send a packet to the m500. */
		err = ritual_exch_server(pconn);
		if (err < 0)
		{
			IO_TRACE(3)
				fprintf(stderr, "usb_accept simple/net: "
					"ritual_exch_server() returned %d\n",
					err);
			return -1;
		}
		break;

	    case PCONN_STACK_NONE:
	    case PCONN_STACK_DEFAULT:
		/* XXX - Error */
	    default:
		/* XXX - Indicate error: unsupported protocol */
		return -1;
	}

printf("usb_accept done\n");
	return 0;
}

static int
pconn_usb_close(PConnection *p)
{	
	struct usb_data *u = p->io_private;

	/* I _think_ it's okay to assume that pconn->protocol has been set,
	 * even though this function may have been called before the
	 * connection was completely set up.
	 */

	/* Clean up the protocol stack elements */
	switch (p->protocol)
	{
	    case PCONN_STACK_DEFAULT:	/* Fall through */
	    case PCONN_STACK_FULL:
		dlp_tini(p);
		padp_tini(p);
		slp_tini(p);
		break;

	    case PCONN_STACK_SIMPLE:	/* Fall through */
	    case PCONN_STACK_NET:
		dlp_tini(p);
		netsync_tini(p);
		break;

	    default:
		/* Do nothing silently: the connection might not have been
		 * completely set up.
		 */
		break;
	}

	if (u->dev)
		usb_close(u->dev);

	if (u != NULL)
		free((void *)u);

	return 0;
}

static int
usb_select(PConnection *p, pconn_direction which, struct timeval *tvp)
{
	return 1;
}

static int
usb_drain(PConnection *p)
{
	/* We don't check p->protocol, because there's nothing to do for
	 * any of them.
	 */
	return 0;
}

int
pconn_libusb_open(PConnection *pconn,
		const char *device,
		const pconn_proto_t protocol)
{
	struct usb_data *u;
	int i, ret;
	unsigned char usbresponse[50];
	UsbConnectionInfoType ci;

	struct usb_bus *bus;
	struct usb_device *dev;

	if (protocol == PCONN_STACK_DEFAULT)
		pconn->protocol = PCONN_STACK_FULL;
	else
		pconn->protocol = protocol;

	/* Initialize the various protocols that the serial connection will
	 * use.
	 */
	switch (pconn->protocol)
	{
	    case PCONN_STACK_FULL:
		/* Initialize the SLP part of the PConnection */
		if (slp_init(pconn) < 0) {
			free(pconn);
			return -1;
		}

		/* Initialize the PADP part of the PConnection */
		if (padp_init(pconn) < 0) {
			padp_tini(pconn);
			slp_tini(pconn);
			return -1;
		}

		/* Initialize the DLP part of the PConnection */
		if (dlp_init(pconn) < 0) {
			dlp_tini(pconn);
			padp_tini(pconn);
			slp_tini(pconn);
			return -1;
		}
		break;

	    case PCONN_STACK_SIMPLE:	/* Fall through */
	    case PCONN_STACK_NET:
		/* Initialize the DLP part of the PConnection */
		if (dlp_init(pconn) < 0) {
			dlp_tini(pconn);
			return -1;
		}

		/* Initialize the NetSync part of the PConnnection */
		if (netsync_init(pconn) < 0) {
			dlp_tini(pconn);
			netsync_tini(pconn);
			return -1;
		}
		break;

	    case PCONN_STACK_NONE:
	    case PCONN_STACK_DEFAULT:
		/* XXX - Error */
	    default:
		/* XXX - Indicate error: unsupported protocol */
		return -1;
	}

	/* Set the methods used by the USB connection */
	pconn->io_bind = &usb_bind;
	pconn->io_read = &usb_read;
	pconn->io_write = &usb_write;
	pconn->io_connect = &usb_connect;
	pconn->io_accept = &usb_accept;
	pconn->io_close = &pconn_usb_close;
	pconn->io_select = &usb_select;
	pconn->io_drain = &usb_drain;

	u = pconn->io_private = malloc(sizeof(struct usb_data));
	if (!u)
		return -1;

	bzero((void *) pconn->io_private, sizeof(struct usb_data));

	/*
	 *  Prompt for the Hot Sync button now, as the USB bus
	 *  enumerator won't create the underlying device we want to
	 *  open until that happens.  The act of starting the hot sync
	 *  operation on the Visor logically plugs it into the USB
	 *  hub port, where it's noticed and enumerated.
	 */
	if (pconn->flags & PCONNFL_PROMPT)
		printf(_("Please press the HotSync button.\n"));

	/*
	 *  We've got to loop trying to open the USB device since
	 *  you'll get an ENXIO until the device has been inserted
	 *  on the USB bus.
	 */

	usb_init();

	dev = NULL;
	for (i = 0; i < 30; i++)
	{
		struct usb_bus *busses;

		usb_find_busses();
		usb_find_devices();

		busses = usb_get_busses();

		for (bus = busses; bus; bus = bus->next) {
			for (dev = bus->devices; dev; dev = dev->next)
			{
				if (dev->descriptor.idVendor == 0x082d &&
					dev->descriptor.idProduct == 0x0100) 
						break;
			}
		}

		if (dev)
			break;

		sleep(1);
	}

#if 0

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
#endif

	/*
	 *  Open the control endpoint of the USB device.  We'll use this
	 *  to figure out if the device in question is the one we are
	 *  interested in and understand, and then to configure it in
	 *  preparation of doing I/O for the actual hot sync operation.
	 */
	if (!dev || !(u->dev = usb_open(dev))) {
		fprintf(stderr, _("%s: Can't open USB device.\n"),
			"pconn_usb_open");
		if (dev)
			perror("open");

		free(u);
		u = pconn->io_private = NULL;

		dlp_tini(pconn);
		padp_tini(pconn);
		slp_tini(pconn);
		return -1;
	}


	/* XXX - Try using ioctl(USB_GET_DEVICE_DESC) to get all sorts of
	 * juicy tidbits about the device, possibly even including its
	 * serial number.
	 */

	/*
	 *  Happily, all of the multibyte values in the struct usb_device_info
	 *  are in host byte order, and don't need to be converted.
	 */
	IO_TRACE(1) {
		fprintf(stderr,
			"USB device information: vendor %04x product %04x\n",
			dev->descriptor.idVendor,
			dev->descriptor.idProduct);
 
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
	if (usb_set_configuration(u->dev, 1) < 0)
		perror("warning: usb_set_configuration failed");

	if (usb_claim_interface(u->dev, 0) < 0)
		perror("warning: usb_claim_interface failed");

	/*
	 *  Now, ask the device (which we believe to be a Handspring Visor)
	 *  about the various USB endpoints which we can make a connection 
	 *  to.  Obviously, here we're looking for the endpoint associated 
	 *  with the Hotsync running on the other end.  This has been observed
	 *  to be endpoint "2", but this is not statically defined, and might
	 *  change based on what other applications are running and perhaps
	 *  on future hardware platforms.
	 */

	bzero((void *)&ci, sizeof(ci));
	if (usb_control_msg(u->dev, USB_TYPE_VENDOR | USB_RECIP_ENDPOINT | 0x80,
		usbRequestVendorGetConnectionInfo, 0, 0, (char *)&ci, 18, 5000) < 0)
	{
		perror(_("usb_control_msg(usbRequestVendorGetConnectionInfo) failed"));

		usb_close(u->dev);

		free((void *)u);
		u = pconn->io_private = NULL;

		dlp_tini(pconn);
		padp_tini(pconn);
		slp_tini(pconn);
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

	/* XXX Fix endian for ci.numPorts */
	for (i = 0; i < ci.numPorts; i++) {
		IO_TRACE(2)
		fprintf(stderr,
			"ConnectionInfo: entry %d function %s on port %d\n",
			i, 
			(ci.connections[i].portFunctionID <= hs_usbfun_MAX)
			  ? hs_usb_functions[ci.connections[i].portFunctionID]
			  : "unknown",
			ci.connections[i].port);

		if (ci.connections[i].portFunctionID == hs_usbfun_Hotsync)
			u->hotsync_ep = ci.connections[i].port;
	}

	if (u->hotsync_ep < 0) {
		fprintf(stderr,
			_("%s: Could not find HotSync endpoint on Visor.\n"),
			"PConnection_usb");
		usb_close(u->dev);
		free((void *)u);
		u = pconn->io_private = NULL;

		dlp_tini(pconn);
		padp_tini(pconn);
		slp_tini(pconn);
		return -1;
	}

	ret = usb_control_msg(u->dev, USB_TYPE_VENDOR | USB_RECIP_ENDPOINT | 0x80,
		usbRequestVendorGetBytesAvailable, 0, 5, (char *)usbresponse, 2, 5000);

	if (ret < 0)
		perror(_("usb_control_msg(usbRequestVendorGetBytesAvailable) failed"));

	IO_TRACE(2) {
		if (ret > 0) {
			for (i = 0; i < ret; i++)
				fprintf(stderr, " 0x%02x", usbresponse[i]);
 
 			fprintf(stderr, "\n");
 		}
	}

	if ((usbresponse[0] | (usbresponse[1] << 8)) != 1) {
		fprintf(stderr,
			_("%s: unexpected response %d to "
			"GetBytesAvailable.\n"),
			"PConnection_usb", usbresponse[0] | (usbresponse[1] << 8));
	}

	/* ------------------------------------------------------------------ 
	 * 
	 *  Ok, all of the device specific control messages have been
	 *  completed.  It is critically important that these be performed
	 *  before opening a full duplex connection to the hot sync
	 *  endpoint on the Visor, on which all of the data transfer occur.
	 *  If this is open while the set configuration operation above is
	 *  done, at best it won't work (in FreeBSD 4.0 and later), and at
	 *  worst it will cause a panic in your kernel.. (pre FreeBSD 4.0)
	 *
	 * ---------------------------------------------------------------- */

	/* Make it so */
	return 1;
}

#endif	/* WITH_USB */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
