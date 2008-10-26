

#include <stdio.h>
#include <poll.h>
#include <unistd.h>

#include <strings.h>

#include <sys/select.h>
#include <sys/types.h>

#include <libusb-1.0/libusb.h>

#include "pconn/ids.h"

int nfd = 0;

static libusb_device_handle *
find_and_open_device(void)
{
	int err;
	libusb_device **list;
	libusb_device *found = NULL;
	libusb_device_handle *handle = NULL;

	size_t cnt = libusb_get_device_list(NULL, &list);
	size_t i = 0;

	if (cnt < 0)
		return NULL;

	for (i = 0; i < cnt; i++) {
		libusb_device *dev = list[i];
		struct libusb_device_descriptor desc;

		int r = libusb_get_device_descriptor(dev, &desc);
                if (r < 0) {
                        fprintf(stderr, "failed to get device descriptor");
                        return NULL;
                }

		/* XXX mmust check for the devices, not the vendors */
		switch (desc.idVendor) {
			case PALM_VENDOR_ID:
			case SONY_VENDOR_ID:
			case HANDSPRING_VENDOR_ID:
			case ACEECA_VENDOR_ID:
			case GARMIN_VENDOR_ID:
				found = dev;
        			break;
    		}
	}

	if (found) {
		err = libusb_open(found, &handle);
		if (err != 0) {
			handle = NULL;
		}
	}

	libusb_free_device_list(list, 1);

	return handle;
}

void callback(struct libusb_transfer *transfer)
{
	int rc = 0;

	if (transfer->status == LIBUSB_TRANSFER_COMPLETED) {
		rc = fwrite(transfer->buffer, transfer->actual_length, 1, stdout);
		fflush(stdout);

		rc = libusb_submit_transfer(transfer);

	} else if (transfer->status == LIBUSB_TRANSFER_TIMED_OUT) {
		rc = libusb_submit_transfer(transfer);
	}

 	if (rc != 0) {
		/* XXX set some glob? */
	}
}

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

#define usbRequestVendorGetExtConnectionInfo 0x04

int probe_endpoints(libusb_device_handle *handle, int *ep_in, int *ep_out)
{
	int i, rc;
	UsbExtConnectionInfoType extci;
	
	bzero((void *)&extci, sizeof(UsbExtConnectionInfoType));

	rc = libusb_control_transfer(handle,
		LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_ENDPOINT | LIBUSB_ENDPOINT_IN,
		usbRequestVendorGetExtConnectionInfo, 0, 0,
		(unsigned char *)&extci, sizeof(UsbExtConnectionInfoType), 3000);

	if (rc < 0) {
		fprintf(stderr, "Failed to query the device for available ports.\n");
		return rc;
	}

	for (i = 0; i < extci.numPorts; i++) {
		if (extci.connections[i].creatorID[0] == '_'
		&& extci.connections[i].creatorID[1] == 'p'
		&& extci.connections[i].creatorID[2] == 'p'
		&& extci.connections[i].creatorID[3] == 'p') {

			if (extci.differentEndPoints) {
				*ep_in = extci.connections[i].info >> 4;
				*ep_out = extci.connections[i].info & 0x0F;
			} else {
				*ep_in = extci.connections[i].port;
				*ep_out = extci.connections[i].port;
			}

			return 0;
		}
	}

	fprintf(stderr, "No PPP port found on the device.\n");

	return -1;
}

int loop(libusb_device_handle *handle, int ep_in, int ep_out)
{
	int rc = 0;
	unsigned char ibuf[64], obuf[64];

	fd_set rfds;

	FD_ZERO(&rfds);

	struct libusb_transfer *rx = libusb_alloc_transfer(0);
	if (rx == NULL)
		return -1; /* XXX check for an appropriate code */

	libusb_fill_bulk_transfer(rx, handle, ep_in | LIBUSB_ENDPOINT_IN,
		ibuf, 64, callback, NULL, 100);

	rc = libusb_submit_transfer(rx);
	if (rc != 0)
		return -1; /* XXX check for an appropriate code */


	while (1) {
		struct timeval tv;

		rc = libusb_get_next_timeout(NULL, &tv);
		if (rc < 0)
			break;

		if (rc == 1 && tv.tv_sec == 0 && tv.tv_usec == 0) {
			libusb_handle_events_timeout(NULL, 0);
			continue;
		}

		if (tv.tv_sec == 0 && tv.tv_usec == 0) {
			tv.tv_sec = 0;
			tv.tv_usec = 100;
		}

		FD_SET(0, &rfds);	/* stdin */
		rc = select(1, &rfds, NULL, NULL, &tv);
		if (rc) {
			int transferred;
			int n = read(0, obuf, 64);
			if (n < 0)
				break;

			rc = libusb_bulk_transfer(handle, ep_out | LIBUSB_ENDPOINT_OUT,
				obuf, n, &transferred, 0);
			if (transferred != n) {
				break;
			}
		} else if (rc < 0) {
			break;
		}

		libusb_handle_events(NULL);
	}

	libusb_cancel_transfer(rx);
	libusb_free_transfer(rx);

	return rc;
}


int main(void)
{
	int rc;

	rc = libusb_init(NULL);
	if (rc == 0) {

		libusb_device_handle *handle;

		fprintf(stderr, "Press 'Connect' on your Palm OS device\n");
		do {
			handle = find_and_open_device();
			sleep(1);
		} while (!handle);

		if (handle) {

			rc = libusb_claim_interface(handle, 0);
			if (rc == 0) {
				int ep_in, ep_out;

				rc = probe_endpoints(handle, &ep_in, &ep_out);
				if (rc == 0)
					rc = loop(handle, ep_in, ep_out);
					/* XXX check error */

				libusb_release_interface(handle, 0);
			} /* XXX else give error */

			libusb_close(handle);
		}
		libusb_exit(NULL);
	}

	return 0;
}
