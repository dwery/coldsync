/* PConnection_serial.c
 *
 * Functions to manipulate serial Palm connections (PConnection).
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: PConnection_serial.c,v 1.6 2000-06-23 11:33:17 arensb Exp $
 */
#include "config.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <termios.h>

#if HAVE_LIBINTL_H
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL_H */

#include "pconn/PConnection.h"

#if !HAVE_CFMAKERAW
extern void cfmakeraw(struct termios *t);
#endif	/* HAVE_CFMAKERAW */

static int
serial_read(struct PConnection *p, unsigned char *buf, int len)
{
	return read(p->fd, buf, len);
}

static int
serial_write(struct PConnection *p, unsigned char *buf, int len)
{
	return write(p->fd, buf, len);
}

static int
serial_drain(struct PConnection *p)
{
	int err;

	err = tcdrain(p->fd);
	if (err < 0)
		perror("tcdrain");

	return err;
}

static int
serial_close(struct PConnection *p)
{	
	return close(p->fd);
}

static int
serial_select(struct PConnection *p,
	      pconn_direction which,
	      struct timeval *tvp) {
	fd_set fds;

	FD_ZERO(&fds);
	FD_SET(p->fd, &fds);

	return (which == forReading) ? select(p->fd+1, &fds, NULL, NULL, tvp)
				     : select(p->fd+1, NULL, &fds, NULL, tvp);
}

static int
serial_setspeed(struct PConnection *pconn, int speed)
{
	int err;
	struct termios term;

	err = tcgetattr(pconn->fd, &term);
	if (err < 0)
	{
		perror("tcgetattr");
		return -1;
	}

	err = cfsetispeed(&term, speed);
	if (err < 0)
	{
		perror("cfsetispeed");
		return -1;
	}

	err = cfsetospeed(&term, speed);
	if (err < 0)
	{
		perror("cfsetospeed");
		return -1;
	}
				/* XXX - Instead of syncing at a constant
				 * speed, should figure out the fastest
				 * speed that the serial port will support.
				 */

	err = tcsetattr(pconn->fd, TCSANOW, &term);
	if (err < 0)
	{
		perror("tcsetattr");
		return -1;
	}

	sleep(1);		/* XXX - Why is this necessary? (under
				 * FreeBSD 3.x). Actually, various sensible
				 * things work without the sleep(), but not
				 * with xcopilot (pseudo-ttys).
				 */

	return 0;
}

int
pconn_serial_open(struct PConnection *p, char *device, int prompt)
{
	struct termios term;

	/*
	 * first things first: open the device.
	 */
	if ((p->fd = open(device, O_RDWR)) < 0) 
		return p->fd;

	IO_TRACE(5)
		fprintf(stderr, "PConnection fd == %d\n", p->fd);

	p->io_read = &serial_read;
	p->io_write = &serial_write;
	p->io_close = &serial_close;
	p->io_select = &serial_select;
	p->io_setspeed = &serial_setspeed;
	p->io_drain = &serial_drain;
	p->io_private = 0;


	/* Set up the terminal characteristics */
	tcgetattr(p->fd, &term);	/* Get current characteristics */

	/* Set initial rate. 9600 bps required for handshaking */
	cfsetispeed(&term, B9600);
	cfsetospeed(&term, B9600);

	cfmakeraw(&term);		/* Make it raw */
	tcsetattr(p->fd, TCSANOW, &term);
					/* Make it so */

	if (prompt)
		printf(_("Please press the HotSync button.\n"));

	return p->fd;
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
