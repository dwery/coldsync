/* PConnection.c
 *
 * Functions to manipulate Palm connections (PConnection).
 *
 * $Id: PConnection.c,v 1.1 1999-01-31 22:24:04 arensb Exp $
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

	/* Open the file */
	if ((pconn->fd = open(fname, O_RDWR)) < 0)
	{
		perror("open");
		return NULL;
	}

	/* Set up the terminal characteristics */
	tcgetattr(pconn->fd, &term);	/* Get current characteristics */
	cfsetspeed(&term, B9600);	/* Set initial rate. 9600 bps required
					 * for handshaking */
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
	free(pconn);	/* XXX - Memory leak: this should ask the
			 * various protocols (from highest to lowest)
			 * to clean up after themselves.
			 */

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
