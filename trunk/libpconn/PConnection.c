/* PConnection.c
 *
 * Functions to manipulate Palm connections (PConnection).
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: PConnection.c,v 1.10 2000-12-10 21:30:33 arensb Exp $
 */
#include "config.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#if HAVE_LIBINTL_H
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL_H */

#include "pconn/PConnection.h"

int	io_trace = 0;

extern int pconn_serial_open(struct PConnection *pconn, char *fname,
			     int prompt_for_hotsync);
extern int serial_close(struct PConnection *p);
extern int pconn_net_open(struct PConnection *pconn, char *fname,
			  int prompt_for_hotsync);
extern int net_close(struct PConnection *p);
#ifdef WITH_USB
extern int pconn_usb_open(struct PConnection *pconn, char *fname,
			  int prompt_for_hotsync);
extern int usb_close(struct PConnection *p);
#endif

/* new_PConnection
 * Opens a new connection on the named port. Returns a handle to the
 * new connection, or NULL in case of error.
 */
struct PConnection *
new_PConnection(char *fname, int listenType, int promptHotSync)
{
	struct PConnection *pconn;	/* New connection */

	/* Allocate space for the new connection */
	if ((pconn = (struct PConnection *) malloc(sizeof(struct PConnection)))
	    == NULL)
	{
		fprintf(stderr, _("Can't allocate new connection\n"));
		return NULL;
	}

	switch (listenType) {
	    case LISTEN_SERIAL:
		/* XXX - Should be able to specify "-" for the filename to
		 * listen on stdin/stdout.
		 */
		if (pconn_serial_open(pconn, fname, promptHotSync) < 0) {
			serial_close(pconn);
			return NULL;
		}
		return pconn;

	    case LISTEN_NET:
		if (pconn_net_open(pconn, fname, promptHotSync) < 0)
		{
			net_close(pconn);
			return NULL;
		}
		return pconn;

/* XXX - If WITH_USB isn't defined, print an informative error message to
 * that effect.
 */
#ifdef WITH_USB
	    case LISTEN_USB:
		/* XXX - Should be able to specify "-" for the filename to
		 * listen on stdin/stdout.
		 */
		if (pconn_usb_open(pconn, fname, promptHotSync) < 0) {
			usb_close(pconn);
			return NULL;
		}
		return pconn;

#endif
	    default:
		fprintf(stderr, _("%s: unknown listen type %d\n"),
			"new_PConnection", listenType);
		free(pconn);
		return NULL;
	}
	/*NOTREACHED*/
}

int
PConnClose(struct PConnection *pconn)
{
	int err;

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

	(*pconn->io_drain)(pconn);

	/* Close the file descriptor and clean up */
	err = (*pconn->io_close)(pconn);

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
PConn_bind(struct PConnection *pconn, struct slp_addr *addr)
{
	return slp_bind(pconn, addr);
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
