/* PConnection.c
 *
 * Functions to manipulate Palm connections (PConnection).
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: PConnection.c,v 1.6 1999-09-05 03:26:48 arensb Exp $
 */
#include "config.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <termios.h>
#include "PConnection.h"
#include "coldsync.h"

#if !HAVE_CFMAKERAW
extern void cfmakeraw(struct termios *t);
#endif	/* HAVE_CFMAKERAW */

/* new_PConnection
 * Opens a new connection on the named port. Returns a handle to the
 * new connection, or NULL in case of error.
 */
struct PConnection *
new_PConnection(char *fname)
{
	struct PConnection *pconn;	/* New connection */
	struct termios term;

	/* Allocate space for the new connection */
	if ((pconn = (struct PConnection *) malloc(sizeof(struct PConnection)))
	    == NULL)
	{
		fprintf(stderr, "Can't allocate new connection\n");
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

	/* Open the file */
	if ((pconn->fd = open(fname, O_RDWR)) < 0)
	{
		fprintf(stderr, "new_PConnection: error opening port \"%s\"\n",
			fname);
		perror("open");
		dlp_tini(pconn);
		padp_tini(pconn);
		slp_tini(pconn);
		free(pconn);
		return NULL;
	}

	/* Set up the terminal characteristics */
	tcgetattr(pconn->fd, &term);	/* Get current characteristics */

	/* Set initial rate. 9600 bps required for handshaking */
	cfsetispeed(&term, B9600);
	cfsetospeed(&term, B9600);

	cfmakeraw(&term);		/* Make it raw */
	tcsetattr(pconn->fd, TCSANOW, &term);
					/* Make it so */

	return pconn;
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
	MISC_TRACE(4)
		fprintf(stderr, "Calling tcdrain()\n");
	tcdrain(pconn->fd);

	/* Clean up the DLP part of the PConnection */
	dlp_tini(pconn);

	/* Clean up the PADP part of the PConnection */
	padp_tini(pconn);

	/* Clean up the SLP part of the PConnection */
	slp_tini(pconn);

	/* Close the file descriptor */
	err = close(pconn->fd);

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
