/* PConnection.c
 *
 * Functions to manipulate Palm connections (PConnection).
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: PConnection.c,v 1.35 2003-05-24 18:06:03 azummo Exp $
 */
#include "config.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#ifndef HAVE_ENODEV
#  define ENODEV	999	/* Some hopefully-impossible value */
#endif  /* HAVE_ENODEV */

#if  HAVE_SYS_SELECT_H
#  include <sys/select.h>		/* To make select() work rationally
					 * under AIX */
#endif	/* HAVE_SYS_SELECT_H */

#if HAVE_STRINGS_H
#  include <strings.h>			/* For bzero() under AIX */
#endif	/* HAVE_STRINGS_H */

#if HAVE_LIBINTL_H
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL_H */

#include <pconn/PConnection.h>

int	io_trace = 0;

extern int pconn_serial_open(PConnection *pconn,
			     const char *fname,
			     const pconn_proto_t protocol);
extern int pconn_net_open(PConnection *pconn,
			  const char *fname,
			  const pconn_proto_t protocol);

#if WITH_USB
extern int pconn_usb_open(PConnection *pconn,
			  const char *fname,
			  const pconn_proto_t protocol);
#endif

#if WITH_LIBUSB
extern int pconn_libusb_open(PConnection *pconn,
			  const char *fname,
			  const pconn_proto_t protocol);
#endif

/* new_PConnection
 * Opens a new connection on the named port. Returns a handle to the
 * new connection, or NULL in case of error.
 */
PConnection *
new_PConnection(char *device,
		const pconn_listen_t listenType,
		const pconn_proto_t protocol,
		const unsigned short flags)
{
	PConnection *pconn;		/* New connection */

	/* Allocate space for the new connection */
	if ((pconn = (PConnection *) malloc(sizeof(PConnection)))
	    == NULL)
	{
		fprintf(stderr, _("Can't allocate new connection.\n"));
		return NULL;
	}

	/* Initialize the common part, if only in case the constructor fails */
	bzero((void *)pconn, sizeof(PConnection));

	pconn->fd		= -1;
	pconn->flags		= flags;
	pconn->io_bind		= NULL;
	pconn->io_read		= NULL;
	pconn->io_write		= NULL;
	pconn->io_connect	= NULL;
	pconn->io_accept	= NULL;
	pconn->io_drain		= NULL;
	pconn->io_close		= NULL;
	pconn->io_select	= NULL;
	pconn->io_private	= NULL;
	pconn->whosonfirst	= 0;
	pconn->speed		= -1;


	/* Initialize callbacks */
	pconn->palm_errno_set_callback = NULL;
	pconn->palm_status_set_callback = NULL;

	switch (listenType) {
	    case LISTEN_SERIAL:
		if (pconn_serial_open(pconn, device, protocol)
		    < 0)
		{
			free(pconn);
			return NULL;
		}
		break;

	    case LISTEN_NET:
		if (pconn_net_open(pconn, device, protocol) < 0)
		{
			free(pconn);
			return NULL;
		}
		break;

	    case LISTEN_USB:
#if WITH_USB
		/* XXX - Should be able to specify "-" for the filename to
		 * listen on stdin/stdout.
		 * USB over stdin/stdout? I don't think that's a good idea :)
		 */
		if (pconn_usb_open(pconn, device, protocol) < 0)
		{
			free(pconn);
			return NULL;
		}
		break;
#else	/* WITH_USB */
		/* This version of ColdSync was built without USB support.
		 * Print a message to this effect. This is done this way
		 * because even without USB support, the parser recognizes
		 *	listen usb {}
		 * the man page talks about it, etc.
		 */
		fprintf(stderr, _("Error: USB support not enabled.\n"));
		free(pconn);
		return NULL;
#endif

	    case LISTEN_LIBUSB:
#if WITH_LIBUSB
		/* XXX - Should be able to specify "-" for the filename to
		 * listen on stdin/stdout.
		 * USB over stdin/stdout? I don't think that's a good idea :)
		 */
		if (pconn_libusb_open(pconn, device, protocol) < 0)
		{
			free(pconn);
			return NULL;
		}
		break;
#else	/* WITH_LIBUSB */
		/* This version of ColdSync was built without libusb support.
		 * Print a message to this effect. This is done this way
		 * because even without USB support, the parser recognizes
		 *	listen libusb {}
		 * the man page talks about it, etc.
		 */
		fprintf(stderr, _("Error: USB through libusb support not enabled.\n"));
		free(pconn);
		return NULL;
#endif
	  
	    default:
		fprintf(stderr, _("%s: unknown listen type %d.\n"),
			"new_PConnection", listenType);
		free(pconn);
		return NULL;
	}

	PConn_set_status(pconn, PCONNSTAT_NONE);
	
	return pconn;
}

int
PConnClose(PConnection *pconn)
{
	int err = 0;

	if (pconn == NULL)
		return 0;

	/* Make sure everything that was sent to the Palm has actually been
	 * sent. Without this, the file descriptor gets closed before
	 * having been properly flushed, so the Palm never gets the final
	 * ACK for the DLP EndOfSync command, and hangs until it times out,
	 * which wastes the user's time.
	 */
	/* XXX - Why does this hang until the Palm times out, under
	 * FreeBSD? (But only with 'xcopilot', it appears.)
	 */
	IO_TRACE(4)
		fprintf(stderr, "Calling io_drain()\n");

	if (pconn->io_drain != NULL)
		PConn_drain(pconn);

	/* Close the file descriptor and clean up
	 * The test is for paranoia, in case it never got assigned.
	 */
	if (pconn->io_close != NULL)
		err = PConn_close(pconn);

	/* This is useful only if a callback has been set, since 
	 * the pconn structure is going to be freed soon.
	 */
	PConn_set_status(pconn, PCONNSTAT_CLOSED);

	/* Free the PConnection */
	free(pconn);

	return err;
}

static void
_PConn_handle_ioerr(struct PConnection *p)
{
	switch (errno)
	{
		case ENODEV:
		case ENXIO:
			p->fd = -1;
			break;
		default:
			break;
	}
}


/* PConn_bind
 * This function is mainly here to try to adhere to the socket API.
 * XXX - For now, it's hardwired to set a SLP address, which is highly
 * bogus.
 */
int
PConn_bind(PConnection *p,
	   const void *addr,
	   const int addrlen)
{
	return (*p->io_bind)(p, addr, addrlen);
}

int
PConn_read(struct PConnection *p,
		unsigned char *buf,
		int len)
{
	int err = (*p->io_read)(p, buf, len);

	if (err < 0)
	{
		_PConn_handle_ioerr(p);
		PConn_set_palmerrno(p, (palmerrno_t) PALMERR_SYSTEM);
	}
	else if (err == 0)
	{
		PConn_set_palmerrno(p, PALMERR_EOF);
	}

	return err;
}

int
PConn_write(struct PConnection *p,
		unsigned const char *buf,
		const int len)
{
	int err = (*p->io_write)(p, buf, len);

	if (err < 0)
	{
		_PConn_handle_ioerr(p);
		PConn_set_palmerrno(p, PALMERR_SYSTEM);
	}

	return err;
}

int
PConn_connect(struct PConnection *p,
		  const void *addr,
		  const int addrlen)
{
	int rc = (*p->io_connect)(p, addr, addrlen);

	if (rc == 0)
		PConn_set_status(p, PCONNSTAT_UP);

	return rc;
}

int
PConn_accept(struct PConnection *p)
{
	int rc = (*p->io_accept)(p);

	if (rc == 0)
		PConn_set_status(p, PCONNSTAT_UP);

	return rc;
}

int
PConn_drain(struct PConnection *p)
{
	return (*p->io_drain)(p);
}

int
PConn_close(struct PConnection *p)
{
	return (*p->io_close)(p);
}

int
PConn_select(struct PConnection *p,
		 pconn_direction direction,
		 struct timeval *tvp)
{
	int err = (*p->io_select)(p, direction, tvp);

	if (err == 0)
	{
	 	/* select() timed out */
	 	PConn_set_palmerrno(p, PALMERR_TIMEOUT);
	 	PConn_set_status(p, PCONNSTAT_LOST);
	}

	return err;
}

palmerrno_t
PConn_get_palmerrno(PConnection *p)
{
	return p->palm_errno;
}

void
PConn_set_palmerrno(PConnection *p, palmerrno_t palm_errno)
{
	if (p->palm_errno_set_callback)
		(*p->palm_errno_set_callback)(p, palm_errno);

	p->palm_errno = palm_errno;
}

void
PConn_set_status(PConnection *p, pconn_stat status)
{
	if (p->palm_status_set_callback)
		(*p->palm_status_set_callback)(p, status);

	p->status = status;
}

pconn_stat
PConn_get_status(PConnection *p)
{
	return p->status;
}

int
PConn_isonline(PConnection *p)
{
	pconn_stat status = PConn_get_status(p);

	return (status == PCONNSTAT_UP) ? 1 : 0;
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
