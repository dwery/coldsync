/* PConnection.c
 *
 * Functions to manipulate Palm connections (PConnection).
 *
 * $Id: PConnection.c,v 1.5 1999-03-16 11:01:49 arensb Exp $
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <termios.h>
#include "config.h"
#include "PConnection.h"

/* XXX - These ought to go someplace else */
#if !HAVE_CFMAKERAW
extern void cfmakeraw(struct termios *t);
#endif	/* HAVE_CFMAKERAW */

/* XXX - Aw, just use cfsetispeed() and cfsetospeed() in the code. */
#if !HAVE_CFSETSPEED
static int
cfsetspeed(struct termios *t, speed_t speed)
{
	cfsetispeed(t, speed);
	return cfsetospeed(t, speed);
}
#endif	/* HAVE_CFSETSPEED */

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
		perror("open");
		dlp_tini(pconn);
		padp_tini(pconn);
		slp_tini(pconn);
		free(pconn);
		return NULL;
	}

	/* Set up the terminal characteristics */
	tcgetattr(pconn->fd, &term);	/* Get current characteristics */
	cfsetspeed(&term, B9600);	/* Set initial rate. 9600 bps required
					 * for handshaking */
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
