/* PConnection_usb.c
 *
 * USB-related stuff for Visors.
 *
 *	Copyright (C) 2000, Louis A. Mamakos.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id$
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
usb_write(PConnection *p, unsigned const char *buf, const int len)
{
	return write(p->fd, buf, len);
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

	return 0;
}

static int
usb_close(PConnection *p)
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

	if (u != NULL)
		free((void *)u);
	return (p->fd >= 0 ? close(p->fd) : 0);
}

static int
usb_select(PConnection *p, pconn_direction which, struct timeval *tvp)
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
usb_drain(PConnection *p)
{
	/* We don't check p->protocol, because there's nothing to do for
	 * any of them.
	 */
	return 0;
}

int
pconn_usb_open(PConnection *pconn,
	       const char *device,
	       const pconn_proto_t protocol)
{
	struct usb_data *u;
	struct usb_device_info udi;
	struct usb_ctl_request ur;
	char *hotsync_ep_name;
	int usb_ep0 = -1, hotsync_endpoint = -1;
	int i;
	unsigned char usbresponse[50];
	UsbConnectionInfoType ci;

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
		if (slp_init(pconn) < 0)
		{
			return -1;
		}

		/* Initialize the PADP part of the PConnection */
		if (padp_init(pconn) < 0)
		{
			padp_tini(pconn);
			slp_tini(pconn);
			return -1;
		}

		/* Initialize the DLP part of the PConnection */
		if (dlp_init(pconn) < 0)
		{
			dlp_tini(pconn);
			padp_tini(pconn);
			slp_tini(pconn);
			return -1;
		}
		break;

	    case PCONN_STACK_SIMPLE:	/* Fall through */
	    case PCONN_STACK_NET:
		/* Initialize the DLP part of the PConnection */
		if (dlp_init(pconn) < 0)
		{
			dlp_tini(pconn);
			return -1;
		}

		/* Initialize the NetSync part of the PConnnection */
		if (netsync_init(pconn) < 0)
		{
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
	pconn->io_close = &usb_close;
	pconn->io_select = &usb_select;
	pconn->io_drain = &usb_drain;

	u = pconn->io_private = malloc(sizeof(struct usb_data));

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

		if ((errno == ENOENT) && ((pconn->flags & PCONNFL_TRANSIENT) != 0))
			/* Just ignore this error */
			;
		else if (errno != ENXIO) {
			fprintf(stderr, _("Error: Can't open \"%s\".\n"),
				device);
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
		fprintf(stderr, _("%s: Can't open USB device.\n"),
			"pconn_usb_open");
		perror("open");
		free(u);
		u = pconn->io_private = NULL;

		dlp_tini(pconn);
		padp_tini(pconn);
		slp_tini(pconn);
		return -1;
	}

	if (ioctl(usb_ep0, USB_GET_DEVICEINFO, &udi)) {
		fprintf(stderr,
			_("%s: Can't get information about USB device.\n"),
			"pconn_usb_open");
		perror("ioctl(USB_GET_DEVICEINFO)");
		(void) close(usb_ep0);
		free((void *)u);
		u = pconn->io_private = NULL;

		dlp_tini(pconn);
		padp_tini(pconn);
		slp_tini(pconn);
		return -1;
	}

#define SURE(x) \
	(((x!=NULL) && (*x !='\0')) ? x : "<not defined>")

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
  "Device information: %s vendor %04x (%s) product %04x (%s) rev %s addr %x\n",
			device,  udi.UDI(vendorNo), SURE(udi.UDI(vendor)), 
			udi.UDI(productNo), SURE(udi.UDI(product)),
			SURE(udi.UDI(release)), udi.UDI(addr));

	}

	if ((udi.UDI(vendorNo) != HANDSPRING_VENDOR_ID) &&
	    (udi.UDI(vendorNo) != PALM_VENDOR_ID))
	{
		fprintf(stderr,
			_("%s: Warning: Unexpected USB vendor ID %#x.\n"),
			"pconn_usb_open", udi.UDI(vendorNo));
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
	bzero((void *) &ur, sizeof(ur));
	ur.UCR(request).bmRequestType = UT_READ_VENDOR_ENDPOINT;
	ur.UCR(request).bRequest = usbRequestVendorGetConnectionInfo;
	USETW(ur.UCR(request).wValue, 0);
	USETW(ur.UCR(request).wIndex, 0);
	USETW(ur.UCR(request).wLength, 18);
	ur.UCR(data) = (void *) &ci;
	ur.UCR(flags) = USBD_SHORT_XFER_OK;
	ur.UCR(actlen) = 0;
	bzero((void *)&ci, sizeof(ci));
	if (ioctl(usb_ep0, USB_DO_REQUEST, &ur) < 0) {
		perror(_("ioctl(USB_DO_REQUEST) usbRequestVendorGetConnectionInfo failed"));
		(void) close(usb_ep0);
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
		u = pconn->io_private = NULL;

		dlp_tini(pconn);
		padp_tini(pconn);
		slp_tini(pconn);
		return -1;	  
	}

	bzero((void *) &ur, sizeof(ur));
	ur.UCR(request).bmRequestType = UT_READ_VENDOR_ENDPOINT;
	ur.UCR(request).bRequest = usbRequestVendorGetBytesAvailable;
	USETW(ur.UCR(request).wValue, 0);
	USETW(ur.UCR(request).wIndex, 5);
	USETW(ur.UCR(request).wLength, 2);
	ur.UCR(data) = &usbresponse[0];
	ur.UCR(flags) = USBD_SHORT_XFER_OK;
	ur.UCR(actlen) = 0;

	if (ioctl(usb_ep0, USB_DO_REQUEST, &ur) < 0) {
		perror(_("ioctl(USB_DO_REQUEST) usbRequestVendorGetBytesAvailable failed"));
	}

	IO_TRACE(2) {
		fprintf(stderr, "first setup 0x1 returns %d bytes: ",
			ur.UCR(actlen));
		for (i = 0; i < ur.UCR(actlen); i++) {
		  fprintf(stderr, " 0x%02x", usbresponse[i]);
		}
		fprintf(stderr, "\n");
	}

	if (UGETW(usbresponse) != 1) {
		fprintf(stderr,
			_("%s: unexpected response %d to "
			  "GetBytesAvailable.\n"),
			"PConnection_usb", UGETW(usbresponse));
	}


	(void) close(usb_ep0);

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


	/*
	 *  Construct the name of the device corresponding to the
	 *  USB endpoint associated with the hot sync service.
	 */
	if ((hotsync_ep_name = malloc(strlen(device)+20)) == NULL) {
		free((void *)u);
		u = pconn->io_private = NULL;

		dlp_tini(pconn);
		padp_tini(pconn);
		slp_tini(pconn);
		return -1;
	}

	sprintf(hotsync_ep_name, "%s.%d", device, hotsync_endpoint);

	IO_TRACE(1)
		fprintf(stderr, "Hotsync endpoint name: \"%s\"\n",
			SURE(hotsync_ep_name));

	/* Under FreeBSD 5.x, the endpoint might not exist yet */
	do {
		pconn->fd = open(hotsync_ep_name, O_RDWR, 0);

		if (pconn->fd < 0)
		{
			if ((errno == ENOENT) &&
			    ((pconn->flags & PCONNFL_TRANSIENT) != 0))
			{
				/* Ignore this error and try again */
				sleep(1);
				continue;
			}

			fprintf(stderr, _("%s: Can't open \"%s\".\n"),
				"pconn_usb_open", hotsync_ep_name);
			perror("open");
			(void) close(usb_ep0);
			free(hotsync_ep_name);
			free((void *)u);
			u = pconn->io_private = NULL;

			dlp_tini(pconn);
			padp_tini(pconn);
			slp_tini(pconn);
			return -1;	  
		}
		break;		/* Success */
	} while (1);

	if ((i = fcntl(pconn->fd, F_GETFL, 0))!=-1) {
		i &= ~O_NONBLOCK;
		fcntl(pconn->fd, F_SETFL, i);
	}

	i = 1;
	if (ioctl(pconn->fd, USB_SET_SHORT_XFER, &i) < 0)
		perror("ioctl(USB_SET_SHORT_XFER)");
  
	free(hotsync_ep_name);

	/* Make it so */
	return pconn->fd;
}

#endif	/* WITH_USB */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
