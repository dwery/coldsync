/*
 * USB system call interface under BSD.
 */

#include "config.h"

/* probably need || defined(__NetBSD__) || defined(__OpenBSD__) */
#if defined(__FreeBSD__)

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <dev/usb/usb.h>

#include "pconn/PConnection.h"

#if HAVE_LIBINTL_H
#include <libintl.h>
#endif /* HAVE_LIBINTL_H */

#include "usb_generic.h"

struct connection {
	uByte	portFunctionID;
	uByte	port;
};

struct usb_connection_info {
	uWord	numPorts;
	struct	connection connections[20];
};

struct usb_data {
	unsigned char iobuf[1024];
	unsigned char *iobufp;
	int iobuflen;
};


/*
 * Open the specified USB device and redirect to the real USB device
 * if required.
 */
int
bsd_usb_open(const char *device, int prompt, void **private)
{
	struct usb_device_info udi;
	struct usb_ctl_request ur;
	struct usb_connection_info ci;
	struct connection *cur_con;
	int fd, hotsync_endpoint, i;
	unsigned char usbresponse[50];
	char *hotsync_ep_name;

	/*
	 * Prompt for the Hot Sync button now, as the USB bus
	 * enumerator won't create the underlying device we want to
	 * open until that happens.  The act of starting the hot sync
	 * operation on the Visor logically plugs it into the USB
	 * hub port, where it's noticed and enumerated.
	 *
	 */
	if (prompt)
		printf(_("Please press the HotSync button.\n"));

	/*
	 * We've got to loop trying to open the USB device since
	 * you'll get an ENXIO until the device has been inserted
	 * on the USB bus.
	 *
	 * Under FreeBSD > 5.0, the named device won't even exist,
	 * so let's go ahead and try to trap that error too.
	 *
	 * XXX
	 * Spinning is really ugly.  It might be best to just
	 * mandate that coldsync be run via usbd.  Or perhaps we
	 * should try to duplicate the the event driven nature
	 * of usbd here (blocking for USB activation events).
	 */
	fd = -1;
	for (i = 0; i < 30; i++) {
		fd = open(device, O_RDWR);
		if (fd != -1)
			break;

		IO_TRACE(1)
			perror(device);

		if (errno != ENXIO && errno != ENOENT) {
			fprintf(stderr, _("Error: Can't open \"%s\".\n"),
			    device);
			perror("open");
			/*
			 * If some other error, don't bother waiting
			 * for the timeout to expire.
			 */
			break;
		}
		sleep(1);
	}

	if (fd == -1) {
		fprintf(stderr, _("%s: Can't open USB device.\n"),
		    "bsd_usb_open");
		perror("open");
		return (-1);
	}

	/*
	 * If we've enabled trace for I/O, then poke the USB kernel
	 * driver to turn on the minimal amount of tracing.  This can
	 * fail if the kernel wasn't built with UGEN_DEBUG defined, so
	 * we just ignore any error which might occur.
	 */
	IO_TRACE(1)
		i = 1;
	else
		i = 0;
	(void)ioctl(fd, USB_SETDEBUG, &i);

	/*
	 * Open the control endpoint of the USB device.  We'll use this
	 * to figure out if the device in question is the one we are
	 * interested in and understand, and then to configure it in
	 * preparation of doing I/O for the actual hot sync operation.
	 */

	if (ioctl(fd, USB_GET_DEVICEINFO, &udi) == -1) {
		fprintf(stderr, _("%s: Can't get information about "
		    "USB device.\n"), "bsd_usb_open");
		perror("ioctl(USB_GET_DEVICEINFO)");
		close(fd);
		return (-1);
	}

#define SURE(x) (((x != NULL) && (*x != '\0')) ? x : "<not defined>")

	/*
	 * Happily, all of the multibyte values in the struct usb_device_info
	 * are in host byte order, and don't need to be converted.
	 */
	IO_TRACE(1)
		fprintf(stderr, "Device information: %s vendor %04x (%s) "
		    "product %04x (%s) rev %s addr %x\n", device,
		    udi.vendorNo, SURE(udi.vendor), udi.productNo,
		    SURE(udi.product), SURE(udi.release), udi.addr);

	if (udi.vendorNo != HANDSPRING_VENDOR_ID && udi.vendorNo !=
	    PALM_VENDOR_ID) {
		fprintf(stderr, _("%s: Warning: Unexpected USB vendor "
		    "ID %#x.\n"), "bsd_usb_open", udi.vendorNo);
	}

	/*
	 * Evenutally, it might be necessary to split out the following
	 * code should another Palm device with a USB peripheral interface
	 * need to be supported in a different way.  Hopefully, they will
	 * simply choose to inherit this existing interface rather than
	 * inventing Yet Another exquisitely round wheel of their own.
	 *
	 * Guess what... [mbd]
	 * Of course, this is a competely different issue from having
	 * different OS level interfacing procedures to USB devices.
	 * Sigh.
	 */

	/*
	 * Ensure that the device is set to the default configuration.
	 * For Visors seen so far, the default is the only one they
	 * support.
	 */

	i = 1;
	if (ioctl(fd, USB_SET_CONFIG, &i) == -1)
		perror("Warning: ioctl(USB_SET_CONFIG) failed");

	/*
	 * Now, ask the device (which we believe to be a USB device
	 * running a PalmOS) about the various USB endpoints which we
	 * can make a connection to.  Obviously, here we're looking
	 * for the endpoint associted with the Hotsync running on the
	 * other end.  This has been observed to be endpoint "2",
	 * but this is not statically defined, and might change
	 * based on what other applications are running and perhaps
	 * on future hardware platforms.
	 */
	bzero((void *)&ur, sizeof(ur));
	ur.request.bmRequestType = UT_READ_VENDOR_ENDPOINT;
	ur.request.bRequest = usbRequestVendorGetConnectionInfo;
	USETW(ur.request.wValue, 0);
	USETW(ur.request.wIndex, 0);
	USETW(ur.request.wLength, 18);
	ur.data = (void *)&ci;
	ur.flags = USBD_SHORT_XFER_OK;
	ur.actlen = 0;
	bzero((void *)&ci, sizeof(ci));
	if (ioctl(fd, USB_DO_REQUEST, &ur) == -1) {
		perror(_("ioctl(USB_DO_REQUEST) "
		    "usbRequestVendorGetConnectionInfo failed"));
		close(fd);
		return (-1);
	}

	/*
	 * Now search the list of functions supported over the USB interface
	 * for the endpoint associated with the HotSync function.  So far,
	 * this has seend to "always" be on endpoint 2, but this might
	 * change and we use this binding mechanism to discover where it
	 * lives now.  Sort of like the portmap(8) daemon.
	 *
	 * Also, beware: as this is a "raw" USB function, the result we
	 * get is in USB-specific byte order.  This happens to be
	 * little endian, but we should use the accessor macros to
	 * ensure the code continues to run on big endian CPUs too.
	 */
	hotsync_endpoint = -1;
	IO_TRACE(2) {
		for (i = 0; i < UGETW(ci.numPorts); i++) {
			cur_con = &ci.connections[i];
			fprintf(stderr, "ConnectionInfo: entry %d function "
			    "%s on port %d\n", i,
			    (cur_con->portFunctionID <= HS_USBFUN_MAX) ?
			    HS_usb_functions[cur_con->portFunctionID] :
			    "unknown", cur_con->port);
		}
		fprintf(stderr, "No more connections\n");
	}
	for (i = 0; i < UGETW(ci.numPorts); i++) {
		cur_con = &ci.connections[i];
		IO_TRACE(2)
			fprintf(stderr, "ConnectionInfo: entry %d function "
			    "%s on port %d\n", i,
			    (cur_con->portFunctionID <= HS_USBFUN_MAX) ?
			    HS_usb_functions[cur_con->portFunctionID] :
			    "unknown", cur_con->port);
		if (cur_con->portFunctionID == HS_USBFUN_HOTSYNC)
			hotsync_endpoint = cur_con->port;
	}

	if (hotsync_endpoint == -1) {
		fprintf(stderr, _("%s: Could not find HotSync endpoint.\n"),
		    "bsd_usb_open");
		close(fd);
		return (-1);
	}

	bzero((void *)&ur, sizeof(ur));
	ur.request.bmRequestType = UT_READ_VENDOR_ENDPOINT;
	ur.request.bRequest = usbRequestVendorGetBytesAvailable;
	USETW(ur.request.wValue, 0);
	USETW(ur.request.wIndex, 5);
	USETW(ur.request.wLength, 2);
	ur.data = &usbresponse[0];
	ur.flags = USBD_SHORT_XFER_OK;
	ur.actlen = 0;

	if (ioctl(fd, USB_DO_REQUEST, &ur) == -1)
		perror(_("ioctl(USB_DO_REQUEST) "
		    "usbRequestVendorGetBytesAvailable"));

	IO_TRACE(2) {
		fprintf(stderr, "first setup 0x1 returns %d bytes: ",
		    ur.actlen);
		for (i = 0; i < ur.actlen; i++)
			fprintf(stderr, " 0x%02x", usbresponse[i]);
		fprintf(stderr, "\n");
	}

	if (UGETW(usbresponse) != 1)
		fprintf(stderr, _("%s: unexpected response %d to "
		    "GetBytesAvailable.\n"), "bsd_usb_open",
		    UGETW(usbresponse));

	close (fd);

	/* ------------------------------------------------------------------
	 *
	 *  Ok, all of the device specific control messages have been
	 *  completed.  It is critically important that these be performed
	 *  before opening a full duplex conneciton to the hot sync
	 *  endpoint on the device, on which all of the data transfer occur.
	 *  If this is open while the set configuration operation above is
	 *  done, at best it won't work (in FreeBSD 4.0 and later), and at
	 *  worst it will cause a panic in your kernel.. (pre FreeBSD 4.0)
	 *
	 * ---------------------------------------------------------------- */


	/*
	 * Consruct the name of the device corresponding to the
	 * USB endpoint associated with the hot sync service.
	 */
	hotsync_ep_name = malloc(strlen(device) + 20);
	if (hotsync_ep_name == NULL)
		return (-1);

	snprintf(hotsync_ep_name, strlen(device) + 20, "%s.%d", device,
	    hotsync_endpoint);

	IO_TRACE(1)
		fprintf(stderr, "Hotsync endpoint name: \"%s\"\n",
		    SURE(hotsync_ep_name));

	fd = open(hotsync_ep_name, O_RDWR, 0);
	if (fd == -1) {
		fprintf(stderr, _("%s: Can't open \"%s\".\n"),
		    "bsd_usb_open", hotsync_ep_name);
		perror("open");
		free(hotsync_ep_name);
		return (-1);
	}
	free(hotsync_ep_name);

	i = fcntl(fd, F_GETFL, 0);
	if (i != -1) {
		i &= ~O_NONBLOCK;
		fcntl(fd, F_SETFL, i);
	}

	i = 1;
	if (ioctl(fd, USB_SET_SHORT_XFER, &i) == -1)
		perror("ioctl(USB_SET_SHORT_XFER)");

	*private = malloc(sizeof(struct usb_data));
	if (*private == NULL) {
		perror("malloc");
		close(fd);
		return (-1);
	}
	bzero(*private, sizeof(struct usb_data));

	return (fd);
}

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
int
bsd_usb_read(int fd, unsigned char *buf, int len, void *private)
{
	struct usb_data *u;
	int copy_len, retlen;

	retlen = 0;
	u = (struct usb_data *)private;

	do {
		/* return leftover stuff from a previous read */
		if (u->iobuflen > 0) {
			copy_len = (len > u->iobuflen) ? u->iobuflen : len;

			bcopy(u->iobufp, buf, copy_len);
			u->iobufp += copy_len;
			u->iobuflen -= copy_len;
			buf += copy_len;
			len -= copy_len;
			retlen += copy_len;
		}

		if (retlen == 0) {
			/*
			 * There wasn't anything left over from the last
			 * read.  Read some new stuff.
			 */
			if (u->iobuflen > 0) {
				fprintf(stderr, _("bsd_usb_read: trying "
				    "to fill a non-empty buffer.\n"));
				abort();
			}

			u->iobufp = u->iobuf;
			u->iobuflen = read(fd, u->iobufp, sizeof(u->iobuf));
			if (u->iobuflen == -1) {
				perror("bsd_usb_read");
				return (-1);
			}
		}
	} while (retlen == 0);

	return (retlen);
}

int
bsd_usb_write(int fd, unsigned const char *buf, const int len,
    void *private)
{

	return (write(fd, buf, len));
}

int
bsd_usb_select(int fd, int for_reading, struct timeval *tvp, void *private)
{
	fd_set fds;
	struct usb_data *u;
	int sel;

	u = (struct usb_data *)private;

	/* If there's buffered read data, then return true. */
	if (for_reading && (u->iobuflen > 0))
		return (1);

	FD_ZERO(&fds);
	FD_SET(fd, &fds);

	/*
	 * So this code looks really good, but in actuallity, the
	 * ugen(4) kernel driver will always return ready.
	 *
	 * Really fixing this would require something horrible,
	 * like interrupting a read with an alarm signal.
	 *
	 * This isn't true for all ugen devices, only those that
	 * use bulk xfer type.  Interrupt and isochronous do the
	 * the right thing. [mbd]
	 */
	if (for_reading)
		sel = select(fd + 1, &fds, NULL, NULL, tvp);
	else
		sel = select(fd + 1, NULL, &fds, NULL, tvp);
	return (sel);
}

int
bsd_usb_close(int fd, void *private)
{

	if (private != NULL)
		free(private);
	return (fd >= 0 ? close(fd) : 0);
}
#else
/* shut up a warning about empty  source */
static const int x = 0;
#endif /* defined(__FreeBSD__) */
