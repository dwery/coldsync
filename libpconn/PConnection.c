/* PConnection.c
 *
 * Functions to manipulate Palm connections (PConnection).
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: PConnection.c,v 1.4.2.1 2000-02-03 04:16:37 arensb Exp $
 */
#include "config.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#if HAVE_LIBINTL
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL */

#include "pconn/PConnection.h"
#include "src/coldsync.h"	/* XXX - Required for LISTEN_USB. This
				 * symbol ought to be defined in a libpconn
				 * file somewhere.
				 */

int	io_trace = 0;

extern int pconn_serial_open(struct PConnection *pconn, char *fname,
			     int prompt_for_hotsync);
#ifdef WITH_USB
extern int pconn_usb_open(struct PConnection *pconn, char *fname,
			  int prompt_for_hotsync);
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

	/* Initialize the SLP part of the PConnection */
	if (slp_init(pconn) < 0)
	{
		free(pconn);
		return NULL;
	}

	/* Initialize the PADP part of the PConnection */
	if (padp_init(pconn) < 0)
	{
		padp_tini(pconn);
		slp_tini(pconn);
		return NULL;
	}

	/* Initialize the DLP part of the PConnection */
	if (dlp_init(pconn) < 0)
	{
		dlp_tini(pconn);
		padp_tini(pconn);
		slp_tini(pconn);
		return NULL;
	}

	switch (listenType) {
	case LISTEN_SERIAL:
		if (pconn_serial_open(pconn, fname, promptHotSync) < 0) {
			break;
		}
		return pconn;

#ifdef WITH_USB
	case LISTEN_USB:
		if (pconn_usb_open(pconn, fname, promptHotSync) < 0) {
			break;
		}
		return pconn;

#endif
	default:
		fprintf(stderr, _("%s: unknown listen type %d\n"),
			"new_PConnection", listenType);
		dlp_tini(pconn);
		padp_tini(pconn);
		slp_tini(pconn);
		free(pconn);
		return NULL;
	}


	/*
	 * if we fall out of the switch by listen type, then something
	 * has gone horribly wrong.
	 */

	fprintf(stderr, _("%s: error opening port \"%s\"\n"),
		"new_PConnection",
		fname);
	perror("open");
	dlp_tini(pconn);
	padp_tini(pconn);
	slp_tini(pconn);
	free(pconn);
	return NULL;
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
	MISC_TRACE(4)
		fprintf(stderr, "Calling io_drain()\n");

	(*pconn->io_drain)(pconn);

	/* Clean up the DLP part of the PConnection */
	dlp_tini(pconn);

	/* Clean up the PADP part of the PConnection */
	padp_tini(pconn);

	/* Clean up the SLP part of the PConnection */
	slp_tini(pconn);

	/* Close the file descriptor */
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
