/* PConnection_serial.c
 *
 * Functions to manipulate serial Palm connections (PConnection).
 *
 *	Copyright (C) 1999, 2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: PConnection_serial.c,v 1.9 2000-12-10 21:34:41 arensb Exp $
 */
/* XXX - The code to find the maximum speed ought to be in this file. The
 * table of available speeds should be here, not in coldsync.c.
 * pconn_serial_open() should set the device to each speed in turn, to see
 * if that speed is even available, marking all of the available speeds.
 * Then, the part of Connect() that sets the speed can just call
 * pconn->io_setspeed(). This, in turn, accepts a speed of 0 as "as fast as
 * possible".
 * Obviously, the USB and network versions of io_setspeed() should just
 * return 0 for anything.
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
#include "pconn/cmp.h"		/* For serial_accept(), which uses CMP */
#include "pconn/palm_errno.h"	/* For PALMERR_* */

#if !HAVE_CFMAKERAW
extern void cfmakeraw(struct termios *t);
#endif	/* HAVE_CFMAKERAW */

/* XXX - This should be defined elsewhere (e.g., in a config file)
 * (Actually, it should be determined dynamically: try to figure out how
 * fast the serial port can go). The reason there are two macros here is
 * that under Solaris, B19200 != 19200 for whatever reason.
 */
/* These two macros specify the default sync rate. The reason it's so low
 * is that the serial port on a Sun Ultra 1 doesn't want to go any faster.
 * Of course, these are the same people who didn't define B38400 = 38400,
 * so what did you expect?
 */
#define SYNC_RATE		38400L
#define BSYNC_RATE		B38400

/* speeds
 * This table provides a list of speeds at which the serial port might be
 * able to communicate.
 * Its structure seems a bit silly, and it is, but only because there are
 * OSes out there that define the B<speed> constants to have values other
 * than their corresponding numeric values.
 */
static struct {
	udword bps;		/* Speed in bits per second, as used by the
				 * CMP layer.
				 */
	speed_t tcspeed;	/* Value to pass to cfset[io]speed() to set
				 * the speed to 'bps'.
				 */
} speeds[] = {
#ifdef B230400
	{ 230400,	B230400 },
#endif	/* B230400 */
#ifdef B115200
	{ 115200,	B115200 },
#endif	/* B115200 */
#ifdef B76800
	{ 76800,	B76800 },
#endif	/* B76800 */
#ifdef B57600
	{ 57600,	B57600 },
#endif	/* B57600 */
#ifdef B38400
	{ 38400,	B38400 },
#endif	/* B38400 */
#ifdef B28800
	{ 28800,	B28800 },
#endif	/* B28800 */
#ifdef B19200
	{ 19200,	B19200 },
#endif	/* B19200 */
#ifdef B14400
	{ 14400,	B14400 },
#endif	/* B14400 */
#ifdef B9600
	{  9600,	 B9600 },
#endif	/* B9600 */
#ifdef B7200
	{  7200,	 B7200 },
#endif	/* B7200 */
#ifdef B4800
	{  4800,	 B4800 },
#endif	/* B4800 */
#ifdef B2400
	{  2400,	 B2400 },
#endif	/* B2400 */
#ifdef B1200
	{  1200,	 B1200 },
#endif	/* B1200 */
	/* I doubt anyone wants to go any slower than 1200 bps */
};
#define num_speeds	sizeof(speeds) / sizeof(speeds[0])

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
serial_accept(struct PConnection *pconn)
{
	/* XXX - Perhaps most of this stuff ought to be moved into a new
	 * function cmp_accept() or some such, which both
	 * PConnection_serial and PConnection_usb can call.
	 */
	int err;
	int i;
	struct cmp_packet cmpp;
	udword bps = SYNC_RATE;		/* Connection speed, in bps */
	speed_t tcspeed = BSYNC_RATE;	/* B* value corresponding to 'bps'
					 * (a necessary distinction because
					 * some OSes (hint to Sun!) have
					 * B19200 != 19200.
					 */

	do {
		IO_TRACE(5)
			fprintf(stderr, "===== Waiting for wakeup packet\n");

		err = cmp_read(pconn, &cmpp);
		if (err < 0)
		{
			if (palm_errno == PALMERR_TIMEOUT)
				continue;
			fprintf(stderr, _("Error during cmp_read: (%d) %s\n"),
				palm_errno,
				_(palm_errlist[palm_errno]));
			exit(1); /* XXX */
		}
	} while (cmpp.type != CMP_TYPE_WAKEUP);

	IO_TRACE(5)
		fprintf(stderr, "===== Got a wakeup packet\n");

	/* Find the speed at which to connect.
	 * If the listen block in .coldsyncrc specifies a speed, use that.
	 * If it doesn't (or the speed is set to 0), then go with what the
	 * Palm suggests.
	 */
	if (pconn->speed == 0)
		pconn->speed = cmpp.rate;

	IO_TRACE(3)
		fprintf(stderr, "pconn->speed == %ld\n",
			pconn->speed);

	/* Go through the speed table. Make sure the requested speed
	 * appears in the table: this is to make sure that there is a valid
	 * B* speed to pass to cfsetspeed().
	 */
	for (i = 0; i < num_speeds; i++)
	{
		IO_TRACE(7)
			fprintf(stderr, "Comparing %ld ==? %ld\n",
				speeds[i].bps, pconn->speed);

		if (speeds[i].bps == pconn->speed)
		{
				/* Found it */
			IO_TRACE(7)
				fprintf(stderr, "Found it\n");
			bps = speeds[i].bps;
			tcspeed = speeds[i].tcspeed;
			break;
		}
	}

	if (i >= num_speeds)
	{
		/* The requested speed wasn't found */
		fprintf(stderr, _("Warning: can't set the speed you "
				  "requested (%ld bps).\nUsing "
				  "default (%ld bps)\n"),
			pconn->speed,
			SYNC_RATE);
		pconn->speed = 0L;
	}

	if (pconn->speed == 0)
	{
		/* Either the .coldsyncrc didn't specify a speed, or else
		 * the one that was specified was bogus.
		 */
		IO_TRACE(2)
			fprintf(stderr, "Using default speed (%ld bps)\n",
				SYNC_RATE);
		bps = SYNC_RATE;
		tcspeed = BSYNC_RATE;
	}
	IO_TRACE(2)
		fprintf(stderr, "-> Setting speed to %ld (%ld)\n",
			(long) bps, (long) tcspeed);

	/* Compose a reply */
	/* XXX - This ought to be in a separate function in cmp.c */
	cmpp.type = CMP_TYPE_INIT;
	cmpp.ver_major = CMP_VER_MAJOR;
	cmpp.ver_minor = CMP_VER_MINOR;
	if (cmpp.rate != bps)
	{
		cmpp.rate = bps;
		cmpp.flags = CMP_IFLAG_CHANGERATE;
	}

	IO_TRACE(5)
		fprintf(stderr, "===== Sending INIT packet\n");
	cmp_write(pconn, &cmpp);	/* XXX - Error-checking */

	IO_TRACE(5)
		fprintf(stderr, "===== Finished sending INIT packet\n");

	/* Change the speed */
	/* XXX - This probably goes in Pconn_accept() or something */

	if ((err = (*pconn->io_setspeed)(pconn, tcspeed)) < 0)
	{
		fprintf(stderr, _("Error trying to set speed"));
		return -1;
	}

	return 0;
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

int
serial_close(struct PConnection *p)
{
	/* Clean up the protocol stack elements */
	dlp_tini(p);
	padp_tini(p);
	slp_tini(p);

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

#if defined(__FreeBSD__)
	/* XXX - For some reason, under FreeBSD, when syncing with xcopilot
	 * (pseudo-ttys), no communication occurs after this point unless
	 * this sleep() is present.
	 */
	sleep(1);
#endif	/* __FreeBSD__ */

	return 0;
}

int
pconn_serial_open(struct PConnection *pconn, char *device, int prompt)
{
	struct termios term;

	/* Initialize the various protocols that the serial connection will
	 * use.
	 */
	/* Initialize the SLP part of the PConnection */
	if (slp_init(pconn) < 0)
	{
		free(pconn);
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

	/* Set the methods used by the serial connection */
	pconn->io_read = &serial_read;
	pconn->io_write = &serial_write;
fprintf(stderr, "set io_write to &serial_write\n");
	pconn->io_accept = &serial_accept;
	pconn->io_close = &serial_close;
	pconn->io_select = &serial_select;
	pconn->io_setspeed = &serial_setspeed;
	pconn->io_drain = &serial_drain;
	pconn->io_private = 0;

	/* Open the device. */
	if ((pconn->fd = open(device, O_RDWR | O_BINARY)) < 0) 
		return pconn->fd;

	IO_TRACE(5)
		fprintf(stderr, "PConnection fd == %d\n", pconn->fd);

	/* Set up the terminal characteristics */
	tcgetattr(pconn->fd, &term);	/* Get current characteristics */

	/* Set initial rate. 9600 bps required for handshaking */
	cfsetispeed(&term, B9600);
	cfsetospeed(&term, B9600);

	cfmakeraw(&term);		/* Make it raw */
	tcsetattr(pconn->fd, TCSANOW, &term);
					/* Make it so */

	if (prompt)
		printf(_("Please press the HotSync button.\n"));

	return pconn->fd;
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
