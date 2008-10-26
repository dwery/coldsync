/* PConnection_libusb.c
 *
 * libusb support.
 *
 *	Copyright (C) 2000, Louis A. Mamakos.
 *	Copyright (C) 2002-08, Alessandro Zummo.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: PConnection_libusb.c,v 1.10 2005-04-15 18:22:30 arensb Exp $
 */

#include "config.h"

#include <stdio.h>	/* This is left outside of the #if WITH_LIBUSB solely
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
#include "pconn/ids.h"

/* This one seems to fail when set too high with some prcs and some
 * devices (max 128 on Tungsten E and 'Saved Preferences', so let's
 * stay safe.
 * XXX Might be worthwile to check the usb max packet size.
 */
#define IOBUF_LEN 64

struct usb_data {
	usb_dev_handle *dev;
	int ep_out;
	int ep_in;
	int ep_int;
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

/* Similar to the previous one, is used on newer devices.
 */
#define usbRequestVendorGetExtConnectionInfo		0x04


typedef struct {
	unsigned short numPorts;
	struct {
		unsigned char	portFunctionID;
		unsigned char	port;
	} connections[20];
} UsbConnectionInfoType, * UsbConnectionInfoPtr;

typedef struct {
	unsigned char numPorts;
	unsigned char differentEndPoints;

	unsigned short reserved;

	struct {
		unsigned char creatorID[4];
		unsigned char port;
		unsigned char info;
		unsigned short reserved;
	} connections[2];
} UsbExtConnectionInfoType, * UsbExtConnectionInfoPtr;

#define	hs_usbfun_Generic	0
#define	hs_usbfun_Debugger	1
#define	hs_usbfun_Hotsync	2
#define	hs_usbfun_Console	3
#define	hs_usbfun_RemoteFileSys	4
#define	hs_usbfun_MAX		4


/* XXX - This doesn't seem to ever be used. Perhaps it should be retained
 * to make debugging messages prettier (in which case it should be moved
 * closer to the hs_usbfun_* definitions) or something.
 */
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
libusb_bind(PConnection *pconn,
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
libusb_read(PConnection *p, unsigned char *buf, int len)
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

			/* The interrupt endpoint, if present, must be emptied 
			 * before reading from the bulk one.
			 */

			do {
				char intbuf[8];

				u->iobuflen = usb_interrupt_read(u->dev, u->ep_int | 0x80,
					&intbuf[0], 8, 10);
			}
			while (u->iobuflen > 0);

			u->iobufp = u->iobuf;
			IO_TRACE(5)
				fprintf(stderr, "calling usb_bulk_read(%02x)\n", u->ep_in | 0x80);
			u->iobuflen = usb_bulk_read(u->dev, u->ep_in | 0x80,
				(char *)u->iobufp, sizeof(u->iobuf), 5000);
			IO_TRACE(5)
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
libusb_write(PConnection *p, unsigned const char *buf, const int len)
{
	struct usb_data *u = p->io_private;
	int ret;

	ret = usb_bulk_write(u->dev, u->ep_out, (char *)buf, len, 5000);
	IO_TRACE(5)
		fprintf(stderr, "usb write %d, ret %d\n", len, ret);
	if (ret < 0)
		fprintf(stderr, "usb write: %s\n", usb_strerror());
	return ret;
}

static int
libusb_connect(PConnection *p, const void *addr, const int addrlen)
{
	return -1;		/* Not applicable to USB connection */
}

static int
libusb_accept(PConnection *pconn)
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
		IO_TRACE(3)
			fprintf(stderr, "usb_accept full\n");

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
		 * XXX m505 works fine, maybe this is an old comment...
		 */
		IO_TRACE(3)
			fprintf(stderr, "usb_accept simple/net\n");

		/* XXX - It seems to help here to send a packet to the m500. */
		/* XXX - Same as the previous note */
		err = ritual_exch_server(pconn);
		if (err < 0)
		{
			IO_TRACE(2)
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
libusb_close(PConnection *p)
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
libusb_select(PConnection *p, pconn_direction which, struct timeval *tvp)
{
	return 1;
}

static int
libusb_drain(PConnection *p)
{
	/* We don't check p->protocol, because there's nothing to do for
	 * any of them.
	 */
	return 0;
}

static int
libusb_probe_os3(struct usb_data *u,
		struct usb_device *dev)
{
	int ret, i;
	UsbConnectionInfoType ci;

	bzero((void *) &ci, sizeof(UsbExtConnectionInfoType));

	ret = usb_control_msg(u->dev, USB_TYPE_VENDOR | USB_RECIP_ENDPOINT | 0x80,
		usbRequestVendorGetConnectionInfo, 0, 0,
		(char *)&ci, sizeof(UsbConnectionInfoType) , 5000);

	if (ret < 0)
	{
		perror(_("usb_control_msg(usbRequestVendorGetConnectionInfo) failed"));
		return 0;
	}

	IO_TRACE(1) {
		fprintf(stderr,
			"GetConnectionInfo:\n"
			"     ports: %d\n",
			ci.numPorts
		);
	}

	for (i = 0; i < ci.numPorts; i++)
	{
		IO_TRACE(1) {
			fprintf(stderr,
				"   port[%d]: %d\n"
				"   func[%d]: %x\n",
				i, ci.connections[i].port,
				i, ci.connections[i].portFunctionID
			);
 		}
	}

	/* Search for the port with HotSync function */

	for (i = 0; i < ci.numPorts; i++)
	{
		if (ci.connections[i].portFunctionID == hs_usbfun_Hotsync)
		{
			u->ep_in  = ci.connections[i].port;
			u->ep_out = ci.connections[i].port;
			u->ep_int = 0;
		}
	}

	if (u->ep_in == 0)
	{
		IO_TRACE(1) {
			fprintf(stderr, "No ports found, trying defaults\n");
 		}

	 	u->ep_in  = 2;
		u->ep_out = 2;
		u->ep_int = 0;
	}

	/* Make it so */
	return 1;
}

static int
libusb_probe_generic(struct usb_data *u,
		struct usb_device *dev)
{
	int ret, i;
	UsbExtConnectionInfoType extci;

	bzero((void *) &extci, sizeof(UsbExtConnectionInfoType));

	ret = usb_control_msg(u->dev, USB_TYPE_VENDOR | USB_RECIP_ENDPOINT | 0x80,
		usbRequestVendorGetExtConnectionInfo, 0, 0,
		(char *)&extci, sizeof(UsbExtConnectionInfoType) , 5000);

	if (ret < 0)
	{
		perror(_("usb_control_msg(usbRequestVendorGetExtConnectionInfo) failed"));
		return 0;
	}

	IO_TRACE(1) {
		fprintf(stderr,
			"GetExtConnectionInfo:\n"
			"     ports: %d\n"
			" different: %d\n",
			extci.numPorts,
			extci.differentEndPoints
		);
	}

	for (i = 0; i < extci.numPorts; i++)
	{
		IO_TRACE(1) {
			fprintf(stderr,
				"   port[%d]    : %d\n"
				"   info[%d]    : %x\n"
				"   reserved[%d]: %d\n"
				"   creator[%d] : %c%c%c%c\n",
				i, extci.connections[i].port,
				i, extci.connections[i].info,
				i, extci.connections[i].reserved,
				i,
				extci.connections[i].creatorID[0],
				extci.connections[i].creatorID[1],
				extci.connections[i].creatorID[2],
				extci.connections[i].creatorID[3]
			);
 		}
	}

	/* XXX We are always using the first connection entry */
	/* XXX creator is cnys for hotsync and _ppp for PPP */

	if (extci.numPorts > 0) {
		if (extci.differentEndPoints) {
			u->ep_in  = extci.connections[0].info >> 4;
			u->ep_out = extci.connections[0].info & 0x0F;
			u->ep_int = 0;
		} else {
			u->ep_in  = extci.connections[0].port;
			u->ep_out = extci.connections[0].port;
			u->ep_int = 0;
		}
	} else {
		IO_TRACE(1) {
			fprintf(stderr, "No ports found, trying defaults\n");
 		}


		if (dev->descriptor.idVendor == HANDSPRING_VENDOR_ID &&
			dev->descriptor.idProduct == HANDSPRING_TREO_ID)
		{
		 	u->ep_in  = 4;
			u->ep_out = 3;
			u->ep_int = 2;
		} else {
		 	u->ep_in  = 2;
			u->ep_out = 2;
			u->ep_int = 0;
		}
	}

	/* Make it so */
	return 1;
}

struct usb_device *
pconn_libusb_find_device(void)
{
	struct usb_device *dev;
	struct usb_bus *busses, *bus;

	usb_find_busses();
	usb_find_devices();

	busses = usb_get_busses();

	for (bus = busses; bus; bus = bus->next) {
		for (dev = bus->devices; dev; dev = dev->next)
		{
			/* XXX We should evaluate the devices, not the vendors */

			if (dev->descriptor.idVendor == HANDSPRING_VENDOR_ID)
				break;

			if (dev->descriptor.idVendor == PALM_VENDOR_ID)
				break;

			if (dev->descriptor.idVendor == SONY_VENDOR_ID)
				break;

			if (dev->descriptor.idVendor == ACEECA_VENDOR_ID)
				break;

			if (dev->descriptor.idVendor == GARMIN_VENDOR_ID)
				break;
		}

		if (dev)
			return dev;
	}

	return NULL;
}

int
pconn_libusb_open(PConnection *pconn,
		const char *device,
		const pconn_proto_t protocol)
{
	int i;
	struct usb_data *u;
	struct usb_device *dev;


	/* Set the methods used by the USB connection */
	pconn->io_bind		= &libusb_bind;
	pconn->io_read		= &libusb_read;
	pconn->io_write		= &libusb_write;
	pconn->io_connect	= &libusb_connect;
	pconn->io_accept	= &libusb_accept;
	pconn->io_close		= &libusb_close;
	pconn->io_select	= &libusb_select;
	pconn->io_drain		= &libusb_drain;

	u = pconn->io_private = malloc(sizeof(struct usb_data));
	if (!u)
		return -1;

	bzero((void *) pconn->io_private, sizeof(struct usb_data));

	/*
	 *  Prompt for the Hot Sync button now, as the USB bus
	 *  enumerator won't create the underlying device we want to
	 *  open until that happens.  The act of starting the hot sync
	 *  operation on the PDA logically plugs it into the USB
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

	for (i = 0; i < 30;)
	{
		dev = pconn_libusb_find_device();
		if (dev)
			break;

		sleep(1);
		
		if (!(pconn->flags & PCONNFL_DAEMON))
			i++;
	}

	/*
	 *  Open the control endpoint of the USB device.  We'll use this
	 *  to figure out if the device in question is the one we are
	 *  interested in and understand, and then to configure it in
	 *  preparation of doing I/O for the actual hot sync operation.
	 */
	if (!dev || !(u->dev = usb_open(dev))) {
		fprintf(stderr, _("%s: Can't open USB device.\n"),
			"pconn_libusb_open");
		if (dev)
			perror("open");

		free(u);
		u = pconn->io_private = NULL;

		return -1;
	}

	/* Choice the default protocol. */

	if (protocol == PCONN_STACK_DEFAULT)
	{
		if (dev->descriptor.idVendor == HANDSPRING_VENDOR_ID &&
			dev->descriptor.idProduct == HANDSPRING_VISOR_ID)
			pconn->protocol = PCONN_STACK_FULL;
		else
			pconn->protocol = PCONN_STACK_NET;
	}
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
	 *  Ensure that the device is set to the default configuration.
	 *  For Visors seen so far, the default is the only one they
	 *  support.
	 */
	if (usb_set_configuration(u->dev, 1) < 0)
		perror("warning: usb_set_configuration failed");

	/* XXX call usb_release_interface() somewhere */
	if (usb_claim_interface(u->dev, 0) < 0)
		perror("warning: usb_claim_interface failed");


	/* Probe */
	if (dev->descriptor.idVendor == HANDSPRING_VENDOR_ID &&
		dev->descriptor.idProduct == HANDSPRING_VISOR_ID)
		libusb_probe_os3(u, dev);
	else
		libusb_probe_generic(u, dev);

	/* Make it so */
	return 1;
}

#endif	/* WITH_LIBUSB */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
