/*
 * PConnection_usbm50x.c - Koen Deforche <kdf@irule.be>
 *
 * $Id: PConnection_usbm50x.c,v 1.2 2001-07-26 07:01:28 arensb Exp $
 */
#include "config.h"
#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>

#include "pconn/PConnection.h"
#include "pconn/netsync.h"
#include "pconn/util.h"

static int
usbm50x_bind(PConnection *pconn,
	     const void *addr,
	     const int addrlen)
{
  return 0;
}

static int
usbm50x_read(PConnection *p, unsigned char *buf, int len)
{
  return read(p->fd, buf, len);
}

static int
usbm50x_write(PConnection *p, unsigned const char *buf, const int len)
{
  return write(p->fd, buf, len);
}

static int
usbm50x_accept(PConnection *pconn)
{
  /*
   * perform ritual packet exchange
   */
  int err;
  const ubyte *inbuf;
  uword inlen;

  /* Receive ritual response 1 */
  inlen = ritual_resp1_size;
  err = netsync_read_method(pconn, &inbuf, &inlen, 1);
  IO_TRACE(5)
    {
      fprintf(stderr,
	      "netsync_read(ritual resp 1) returned %d\n",
	      err);
      if (err > 0)
	debug_dump(stderr, "<<<", inbuf, inlen);
    }

  /* Send ritual statement 2 */
  err = netsync_write(pconn, ritual_stmt2, ritual_stmt2_size);
  IO_TRACE(5)
    fprintf(stderr, "netsync_write(ritual stmt 2) returned %d\n",
	    err);

  /* Receive ritual response 2 */
  err = netsync_read(pconn, &inbuf, &inlen);
  IO_TRACE(5)
    {
      fprintf(stderr, "netsync_read returned %d\n", err);
      if (err > 0)
	debug_dump(stderr, "<<<", inbuf, inlen);
    }

  /* Send ritual statement 3 */
  err = netsync_write(pconn, ritual_stmt3, ritual_stmt3_size);
  IO_TRACE(5)
    fprintf(stderr, "netsync_write(ritual stmt 3) returned %d\n",
	    err);

  /* Receive ritual response 3 */
  err = netsync_read(pconn, &inbuf, &inlen);
  IO_TRACE(5)
    {
      fprintf(stderr, "netsync_read returned %d\n", err);
      if (err > 0)
	debug_dump(stderr, "<<<", inbuf, inlen);
    }
  
  return 0;
}

static int
usbm50x_close(PConnection *p)
{
	/* Clean up the protocol stack elements */
	dlp_tini(p);
	netsync_tini(p);

	return (p->fd >= 0 ? close(p->fd) : 0);
}

static int
usbm50x_select(PConnection *p,
	       pconn_direction which,
	       struct timeval *tvp)
{
	fd_set fds;

	FD_ZERO(&fds);
	FD_SET(p->fd, &fds);

	return (which == forReading) ? select(p->fd+1, &fds, NULL, NULL, tvp)
				     : select(p->fd+1, NULL, &fds, NULL, tvp);
}

static int
usbm50x_connect(PConnection *p, const void *addr, const int addrlen)
{
	return -1;		/* Not applicable to serial connection */
}

static int
usbm50x_drain(PConnection *p)
{
  /*
   * XXX Don't know if there's a USB equivalent of flushing a stream
   * or tty connection.
   */
  return 0;
}

/*
 * pconn_usbm50x_open
 * Initialize a new m50x USB connection
 * 'pconn' is a partly-initialized PConnection; it must still be
 * initialized as a m50x USB PConnection.
 * 'device' is the pathname of the usb HotSync port.
 * 'prompt': if set, prompt the user to press the HotSync button.
 */
int
pconn_usbm50x_open(PConnection *pconn, char *device, Bool prompt)
{
	struct termios term;

	/* Initialize the various protocols that the serial connection will
	 * use.
	 */
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

	/* Set the methods used by the network connection */
	pconn->io_bind = &usbm50x_bind;
	pconn->io_read = &usbm50x_read;
	pconn->io_write = &usbm50x_write;
	pconn->io_connect = &usbm50x_connect;
	pconn->io_accept = &usbm50x_accept;
	pconn->io_close = &usbm50x_close;
	pconn->io_select = &usbm50x_select;
	pconn->io_drain = &usbm50x_drain;
	pconn->io_private = 0;

	/* Open the device.
	 * This is rather funky, due to the fact that the Palm as
	 * a USB device doesn't exist until the sync starts, but
	 * under Linux, you use it as if it were a serial port.
	 *
	 * Under Linux, open() returns ENXIO if the device doesn't
	 * exist at all, and ENODEV if it doesn't exist at the
	 * moment. I don't think open() ever returns ENODEV under
	 * any other OS, so this should be okay. Otherwise, it
	 * might be necessary to check the major and/or minor
	 * device numbers under Linux, and add other Linux-specfic
	 * hacks.
	 */
	while (1)
	{
	  	if ((pconn->fd = open(device, O_RDWR | O_BINARY))
			    >= 0)
				break;	/* Okay. Break out of bogus loop */

			switch (errno)
			{
			    case ENODEV:
				fprintf(stderr,
					"Warning: no device on %s. "
					"Sleeping\n",
					device);
				sleep(5);
				continue;

			    default:
				fprintf(stderr,
					"Error: Can't open \"%s\".\n",
					device);
				perror("open");
				dlp_tini(pconn);
				netsync_tini(pconn);
				return pconn->fd;
			}
		}

	/* Set up the terminal characteristics */
	tcgetattr(pconn->fd, &term);	/* Get current characteristics */

	/* Set initial rate. 9600 bps required for handshaking */
	cfsetispeed(&term, B9600);
	cfsetospeed(&term, B9600);

	cfmakeraw(&term);		/* Make it raw */
	/* XXX - Error-checking */
	tcsetattr(pconn->fd, TCSANOW, &term);
	/* XXX - Error-checking */

	if (prompt)
		printf("Please press the HotSync button.\n");

	return pconn->fd;
}
