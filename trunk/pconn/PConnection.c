/* PConnection.c
 *
 * Functions to manipulate Palm connections (PConnection).
 *
 * $Id: PConnection.c,v 1.2 1999-02-21 08:53:51 arensb Exp $
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <termios.h>
#include "PConnection.h"

static struct PConnection *pconn_list = NULL;

/* PConnLookup
 * Given a file descriptor, return the 'struct PConnection' associated
 * with it, or NULL if it can't be found.
 */
struct PConnection *
PConnLookup(int fd)
{
	struct PConnection *cur;

	/* Iterate over the list of all PConnections */
	for (cur = pconn_list; cur != NULL; cur = cur->next)
		/* See if 'cur's file descriptor matches the one we're
		 * looking for.
		 */
		if (cur->fd == fd)
			return cur;

	return NULL;		/* Nope, couldn't find it */
}

/* new_PConnection
 * Opens a new connection on the named port. Returns a handle to the
 * new connection, or NULL in case of error.
 */
int
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

	/* Open the file */
	if ((pconn->fd = open(fname, O_RDWR)) < 0)
	{
		perror("open");
		dlp_tini(pconn);
		padp_tini(pconn);
		slp_tini(pconn);
		free(pconn);
		return -1;
	}

	/* Set up the terminal characteristics */
	tcgetattr(pconn->fd, &term);	/* Get current characteristics */
	cfsetspeed(&term, B9600);	/* Set initial rate. 9600 bps required
					 * for handshaking */
	/* XXX - Solaris 2.5 does not have cfmakeraw() */
	cfmakeraw(&term);		/* Make it raw */
	tcsetattr(pconn->fd, TCSANOW, &term);
					/* Make it so */

	/* Put the new PConnection on the list */
	pconn->next = pconn_list;
	pconn_list = pconn;

	return pconn->fd;
}

int
PConnClose(int fd)
{
	int err;
	struct PConnection *pconn;

	/* Look up the PConnection */
	if ((pconn = PConnLookup(fd)) == NULL)
		return -1;	/* Couldn't find the PConnection */

	/* Clean up the DLP part of the PConnection */
	dlp_tini(pconn);

	/* Clean up the PADP part of the PConnection */
	padp_tini(pconn);

	/* Clean up the SLP part of the PConnection */
	slp_tini(pconn);

	/* Close the file descriptor */
	err = close(pconn->fd);

	/* Take the PConnection off of the master list */
	if (pconn_list == pconn)
	{
		/* 'pconn' is at the head of the list */
		pconn_list = pconn->next;
		pconn->next = NULL;
	} else {
		struct PConnection *it;		/* Iterator */
		for (it = pconn_list; it != NULL; it = it->next)
			if (it->next == pconn)
			{
				/* 'it' is the PConnection just before
				 * 'pconn'. Take 'pconn' out of the
				 * linked list.
				 */
				it->next = pconn->next;
				pconn->next = NULL;
				break;
			}
	}

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
PConn_bind(int fd, struct slp_addr *addr)
{
	return slp_bind(fd, addr);
}
