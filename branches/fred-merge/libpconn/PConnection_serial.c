/* PConnection_serial.c
 *
 * Functions to manipulate serial Palm connections (PConnection).
 *
 *	Copyright (C) 1999, 2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: PConnection_serial.c,v 1.29 2001-07-30 07:22:46 arensb Exp $
 */
#include "config.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>

#ifndef HAVE_ENODEV
#  define ENODEV	999	/* Some hopefully-impossible value */
#endif	/* HAVE_ENODEV */

#if HAVE_LIBINTL_H
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL_H */

#include "pconn/PConnection.h"
#include "pconn/cmp.h"		/* For cmp_accept() */
#include "pconn/netsync.h"	/* For netsync_init() */

#if !HAVE_CFMAKERAW
extern void cfmakeraw(struct termios *t);
#endif	/* HAVE_CFMAKERAW */

static int find_available_speeds(int fd);
static inline int bps_entry(const udword bps);
static int setspeed(PConnection *pconn, int speed);

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
	int usable;		/* Flag: can the serial port go at this
				 * rate? */
	udword bps;		/* Speed in bits per second, as used by the
				 * CMP layer.
				 */
	speed_t tcspeed;	/* Value to pass to cfset[io]speed() to set
				 * the speed to 'bps'.
				 */
} speeds[] = {
#ifdef B230400
	{ 1,	230400,	B230400 },
#endif	/* B230400 */
#ifdef B115200
	{ 1,	115200,	B115200 },
#endif	/* B115200 */
#ifdef B76800
	{ 1,	 76800,	 B76800 },
#endif	/* B76800 */
#ifdef B57600
	{ 1,	 57600,	 B57600 },
#endif	/* B57600 */
#ifdef B38400
	{ 1,	 38400,	 B38400 },
#endif	/* B38400 */
#ifdef B28800
	{ 1,	 28800,	 B28800 },
#endif	/* B28800 */
#ifdef B19200
	{ 1,	 19200,	 B19200 },
#endif	/* B19200 */
#ifdef B14400
	{ 1,	 14400,	 B14400 },
#endif	/* B14400 */
#ifdef B9600
	{ 1,	  9600,	  B9600 },
#else	/* B9600 */
#  error B9600 must be defined for initial handshake.
#endif	/* B9600 */
#ifdef B7200
	{ 1,	  7200,	  B7200 },
#endif	/* B7200 */
#ifdef B4800
	{ 1,	  4800,	  B4800 },
#endif	/* B4800 */
#ifdef B2400
	{ 1,	  2400,	  B2400 },
#endif	/* B2400 */
#ifdef B1200
	{ 1,	  1200,	  B1200 },
#endif	/* B1200 */
	/* I doubt anyone wants to go any slower than 1200 bps */
};
#define num_speeds	sizeof(speeds) / sizeof(speeds[0])

#if !HAVE_USLEEP
/* usleep
 * Sleep for 'usec' microseconds. A reimplementation for those OSes that
 * don't have it.
 */
int
usleep(unsigned int usec)
{
	struct timeval tv;

	tv.tv_sec  = usec / 1000000;
	tv.tv_usec = usec % 1000000;
	return select(0, NULL, NULL, NULL, &tv);
}
#endif	/* HAVE_USLEEP */

/* find_available_speeds
 * Go through the entries in 'speeds[]', and see if the serial port can go
 * at that speed. Update speeds[].usable.
 * Returns 0 if successful, or a negative value in case of error.
 *
 * Note: this function assumes that a) speeds[] is sorted in order of
 * descending speed, and b) if the serial port can go at speed S, then it
 * can go at all slower speeds as well.
 */
static int
find_available_speeds(int fd)
{
	int i;
	int err;
	struct termios term;

	IO_TRACE(3)
		fprintf(stderr, "Discovering available speeds.\n");

	err = tcgetattr(fd, &term);	/* Get current terminal attributes */
	if (err < 0)
		return -1;

	for (i = 0; i < num_speeds; i++)
	{
		if (!speeds[i].usable)
			/* Skip speeds that have been marked unusable.
			 * Presumably, this should never happen, since this
			 * function is supposed to find out, but maybe a
			 * speed has had its 'usable' field initialized to
			 * 0 for future expansion, though we don't want to
			 * use it.
			 */
			continue;

		IO_TRACE(3)
			fprintf(stderr, "Trying %ld bps (%d)... ",
				speeds[i].bps, speeds[i].tcspeed);

		/* Try setting the input speed */
		if ((err = cfsetispeed(&term, speeds[i].tcspeed)) < 0)
		{
			/* Hard to imagine that we'd ever get here */
			IO_TRACE(3)
				fprintf(stderr, "no (cfsetispeed)\n");
			speeds[i].usable = 0;
			continue;
		}

		/* Try setting the output speed */
		if ((err = cfsetospeed(&term, speeds[i].tcspeed)) < 0)
		{
			/* Hard to imagine that we'd ever get here */
			IO_TRACE(3)
				fprintf(stderr, "no (cfsetospeed)\n");
			speeds[i].usable = 0;
			continue;
		}

		/* Make it so */
		if ((err = tcsetattr(fd, TCSANOW, &term)) < 0)
		{
			IO_TRACE(3)
				fprintf(stderr, "no (tcsetattr)\n");
			speeds[i].usable = 0;
			continue;
		}

		IO_TRACE(3)
			fprintf(stderr, "yes\n");
		speeds[i].usable = 1;

		/* If we've gotten this far, then speeds[i] is the fastest
		 * speed at which the serial port can go. Break out of the
		 * loop and mark all remaining speeds as usable.
		 */
		break;
	}

	for (; i < num_speeds; i++)
	{
		IO_TRACE(3)
			fprintf(stderr, "Assuming %ld bps (%d) yes\n",
				speeds[i].bps, speeds[i].tcspeed);
		speeds[i].usable = 1;
	}

	return 0;
}

/* bps_entry
 * Convenience function: given a speed in bps, find the entry in 'speeds[]'
 * for that speed.
 * Returns that entry's index in 'speeds' if successful, or a negative value
 * otherwise.
 */
static inline int
bps_entry(const udword bps)
{
	int i;

	IO_TRACE(6)
		fprintf(stderr, "bps_entry(%ld) == ", bps);

	for (i = 0; i < num_speeds; i++)
		if (speeds[i].bps == bps)
		{
			IO_TRACE(6)
				fprintf(stderr, "%d\n", i);
			return i;
		}

	IO_TRACE(6)
		fprintf(stderr, "-1\n");
	return -1;		/* Couldn't find it */
}

static int
serial_bind(PConnection *pconn,
	    const void *addr,
	    const int addrlen)
{
	switch (pconn->protocol)
	{
	    case PCONN_STACK_FULL:
		return slp_bind(pconn, (const struct slp_addr *) addr);
		break;

	    case PCONN_STACK_SIMPLE:	/* Fall through */
	    case PCONN_STACK_NET:
		return 0;
	    default:
		/* XXX - Indicate error: unsupported protocol stack */
		return -1;
	}
}

static int
serial_read(PConnection *p, unsigned char *buf, int len)
{
	return read(p->fd, buf, len);
}

static int
serial_write(PConnection *p, unsigned const char *buf, const int len)
{
	return write(p->fd, buf, len);
}

static int
serial_accept(PConnection *pconn)
{
	int err;
	int speed_ix;			/* Index into 'speeds[]' */
	udword newspeed;
	speed_t tcspeed = BSYNC_RATE;	/* B* value corresponding to 'bps'
					 * (a necessary distinction because
					 * some OSes (*cough*Sun*cough*
					 * *Linux*cough*) have B19200 !=
					 * 19200.
					 */

	switch (pconn->protocol)
	{
	    case PCONN_STACK_FULL:
		/* Find the speed at which to connect. If the listen block
		 * in .coldsyncrc specifies a speed, use that. If it
		 * doesn't (or the speed is set to 0), then go with what
		 * the Palm suggests.
		 */
		IO_TRACE(3)
			fprintf(stderr, "pconn->speed == %ld\n",
				pconn->speed);

		/* Go through the speed table. Make sure the requested
		 * speed appears in the table: this is to make sure that
		 * there is a valid B* speed to pass to cfsetspeed().
		 */
		if (pconn->speed != 0)
		{
			speed_ix = bps_entry(pconn->speed);
			if (speed_ix < 0)
			{
				/* The requested speed wasn't found */
				fprintf(stderr,
					_("Warning: can't set the speed you "
					  "requested (%ld bps).\nUsing "
					  "default.\n"),
					pconn->speed);
				pconn->speed = 0L;
			}
		}

		newspeed = cmp_accept(pconn, pconn->speed);
		if (newspeed == ~0)
		{
			fprintf(stderr,
				_("Error establishing CMP connection.\n"));
			return -1;
		}

		/* Find 'tcspeed' from 'newspeed' */
		pconn->speed = newspeed;
		speed_ix = bps_entry(newspeed);
		/* XXX - Error-checking */
		tcspeed = speeds[speed_ix].tcspeed;

		/* Change the speed */
		if ((err = setspeed(pconn, tcspeed)) < 0)
		{
			fprintf(stderr, _("Error trying to set speed.\n"));
			return -1;
		}
		break;

	    case PCONN_STACK_SIMPLE:
	    case PCONN_STACK_NET:
		/* Exchange ritual packets */
		err = ritual_exch_server(pconn);
		if (err < 0)
			return -1;
		break;

	    default:
		/* XXX - Indicate error: unsupported protocol */
		return -1;
	}
	return 0;
}

static int
serial_connect(PConnection *p, const void *addr, const int addrlen)
{
	return -1;		/* Not applicable to serial connection */
}

static int
serial_drain(PConnection *p)
{
	int err = 0;

	/* We don't check pconn->protocol here because a) the sequence is
	 * the same for all protocol stacks, and b) it might be incorrect
	 * if this function was called before the connection was completely
	 * set up, so it might be wrong.
	 */

	/* Drop any unsent or unreceived data that might still be lingering
	 * in the buffer.
	 */
	if (p->fd >= 0)
		/* Need to check the file descriptor because this function
		 * is called both for normal and abnormal termination.
		 */
		err = tcdrain(p->fd);
	if (err < 0)
		perror("tcdrain");

	return err;
}

static int
serial_close(PConnection *p)
{
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

	return (p->fd >= 0 ? close(p->fd) : 0);
}

static int
serial_select(PConnection *p,
	      pconn_direction which,
	      struct timeval *tvp) {
	fd_set fds;

	FD_ZERO(&fds);
	FD_SET(p->fd, &fds);

	return (which == forReading) ? select(p->fd+1, &fds, NULL, NULL, tvp)
				     : select(p->fd+1, NULL, &fds, NULL, tvp);
}

/* pconn_serial_open
 * Initialize a new serial connection.
 * 'pconn' is a partly-initialized PConnection; it must still be
 * initialized as a serial PConnection.
 * 'device' is the pathname of the serial port. If it is NULL, use stdin.
 * 'protocol' is the software protcol stack to use: ordinary serial devices
 * use DLP->PADP->SLP, whereas Palm m50x'es use DLP->netsync
 * 'prompt': if set, prompt the user to press the HotSync button.
 */
int
pconn_serial_open(PConnection *pconn,
		  const char *device,
		  const int protocol,
		  const Bool prompt)
{
	struct termios term;

	if (protocol == PCONN_STACK_DEFAULT)
		pconn->protocol = PCONN_STACK_FULL;
	else
		pconn->protocol = protocol;

	/* Initialize the various protocols that the serial connection will
	 * use, according to the requested software protocol stack.
	 */
	switch (pconn->protocol)
	{
	    case PCONN_STACK_FULL:
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
		break;

	    case PCONN_STACK_SIMPLE:
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

	    default:
		/* XXX - Indicate error: unsupported protocol stack */
		return -1;
	}

	/* Set the methods used by the serial connection */
	pconn->io_bind = &serial_bind;
	pconn->io_read = &serial_read;
	pconn->io_write = &serial_write;
	pconn->io_accept = &serial_accept;
	pconn->io_connect = &serial_connect;
	pconn->io_close = &serial_close;
	pconn->io_select = &serial_select;
	pconn->io_drain = &serial_drain;
	pconn->io_private = 0;

	if (device == NULL)
	{
		/* Use stdin */
		pconn->fd = STDIN_FILENO;
	} else {
		/* Open the device.
		 * This is rather funky, due to the fact that the Visor as
		 * a USB device doesn't exist until the sync starts, but
		 * under Linux, you use it as if it were a serial port (and
		 * there doesn't appear to be an equivalent of usbd :-( ).
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
					_("Warning: no device on %s. "
					  "Sleeping\n"),
					device);
				sleep(5);
				continue;

			    default:
				fprintf(stderr,
					_("Error: Can't open \"%s\".\n"),
					device);
				perror("open");
				dlp_tini(pconn);
				padp_tini(pconn);
				slp_tini(pconn);
				return pconn->fd;
			}
		}
	}

	IO_TRACE(5)
		fprintf(stderr, "PConnection fd == %d\n", pconn->fd);

	/* Find the speeds at which the serial port can go */
	if (find_available_speeds(pconn->fd) < 0)
	{
		dlp_tini(pconn);
		padp_tini(pconn);
		slp_tini(pconn);
		return -1;
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
					/* Make it so */

	if (prompt)
		printf(_("Please press the HotSync button.\n"));

	return pconn->fd;
}

static int
setspeed(PConnection *pconn, int speed)
{
	int err;
	struct termios term;

	IO_TRACE(5)
		fprintf(stderr, "Setting serial device speed to %d\n",
			speed);

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

	err = tcsetattr(pconn->fd, TCSANOW, &term);
	if (err < 0)
	{
		perror("tcsetattr");
		return -1;
	}

	/* Some serial drivers need some time to settle down */
	usleep(50000);

	return 0;
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
