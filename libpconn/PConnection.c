/* PConnection.c
 *
 * Functions to manipulate Palm connections (PConnection).
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: PConnection.c,v 1.29 2002-04-27 18:36:31 azummo Exp $
 */
#include "config.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

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

#include "pconn/PConnection.h"

int	io_trace = 0;

extern int pconn_serial_open(PConnection *pconn,
			     const char *fname,
			     const pconn_proto_t protocol,
			     const Bool prompt_for_hotsync);
extern int pconn_net_open(PConnection *pconn,
			  const char *fname,
			  const pconn_proto_t protocol,
			  const Bool prompt_for_hotsync);

#if WITH_USB
extern int pconn_usb_open(PConnection *pconn,
			  const char *fname,
			  const pconn_proto_t protocol,
			  const Bool prompt_for_hotsync);
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
	pconn->fd		= -1;
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

	switch (listenType) {
	    case LISTEN_SERIAL:
		if (pconn_serial_open(pconn, device, protocol, flags)
		    < 0)
		{
			free(pconn);
			return NULL;
		}
		break;

	    case LISTEN_NET:
		if (pconn_net_open(pconn, device, protocol, flags) < 0)
		{
			free(pconn);
			return NULL;
		}
		break;

	    case LISTEN_USB:
#if WITH_USB
		/* XXX - Should be able to specify "-" for the filename to
		 * listen on stdin/stdout.
		 */
		if (pconn_usb_open(pconn, device, protocol, flags) < 0)
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
	  
	    default:
		fprintf(stderr, _("%s: unknown listen type %d.\n"),
			"new_PConnection", listenType);
		free(pconn);
		return NULL;
	}
	
	pconn->status = PCONNSTAT_UP;

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

	/* Free the PConnection */
	free(pconn);

	return err;
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
		PConn_set_palmerrno(p, PALMERR_SYSTEM);
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
		PConn_set_palmerrno(p, PALMERR_SYSTEM);
	}

	return err;
}
			
int
PConn_connect(struct PConnection *p,
		  const void *addr,
		  const int addrlen)
{
	return (*p->io_connect)(p, addr, addrlen);
}

int
PConn_accept(struct PConnection *p)
{
	return (*p->io_accept)(p);
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
PConn_set_palmerrno(PConnection *p, palmerrno_t errno)
{
	p->palm_errno = errno;
}

void
PConn_set_status(PConnection *p, pconn_stat status)
{
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
	return p->status & PCONNSTAT_UP;
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
