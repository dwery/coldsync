/* palmconn.c
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	Copyright (C) 2002, Alessandro Zummo
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: palmconn.c,v 2.1 2002-07-18 16:43:16 azummo Exp $
 */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>		/* For malloc(), atoi() */
#include <fcntl.h>		/* For open() */
#include <sys/param.h>		/* For MAXPATHLEN */
#include <termios.h>		/* Experimental */
#include <dirent.h>		/* For opendir(), readdir(), closedir() */
#include <string.h>		/* For strrchr() */
#include <netdb.h>		/* For gethostbyname2() */
#include <sys/socket.h>		/* For AF_* */
#include <netinet/in.h>		/* For in_addr */
#include <arpa/inet.h>		/* For inet_ntop() and friends */

#if HAVE_STRINGS_H
#  include <strings.h>		/* For strcasecmp() under AIX */
#endif	/* HAVE_STRINGS_H */

#if HAVE_INET_NTOP
#  include <arpa/nameser.h>	/* Solaris's <resolv.h> requires this */
#  include <resolv.h>		/* For inet_ntop() under Solaris */
#endif	/* HAVE_INET_NTOP */
#include <unistd.h>		/* For sleep() */
#include <ctype.h>		/* For isalpha() and friends */
#include <errno.h>		/* For errno. Duh. */

/* Include I18N-related stuff, if necessary */
#if HAVE_LIBINTL_H
#  include <locale.h>		/* For setlocale() and friends */
#  include <libintl.h>
#endif	/* HAVE_LIBINTL_H */

#include "pconn/pconn.h"
#include "cs_error.h"
#include "coldsync.h"
#include "symboltable.h"

/* Connect
 * Wait for a Palm to show up on the other end.
 */
static int
Connect(PConnection *pconn)
{
	struct slp_addr pcaddr;

	pcaddr.protocol = (ubyte) SLP_PKTTYPE_PAD;
					/* XXX - This ought to be part of
					 * the initial socket setup.
					 */
	pcaddr.port = (ubyte) SLP_PORT_DLP;
	PConn_bind(pconn, &pcaddr, sizeof(struct slp_addr));
	if (PConn_accept(pconn) < 0)
	{
		update_cs_errno_p(pconn);
		return -1;
	}

	return 0;
}

static int
Disconnect(PConnection *pconn, const ubyte status)
{
	int err = 0;

	/* Terminate the sync, but check if we still have a connection */
	if (PConn_isonline(pconn))
	{
		err = DlpEndOfSync(pconn, status);
		if (err < 0)
		{
			Error(_("Error during DlpEndOfSync: (%d) %s."),
			      (int) PConn_get_palmerrno(pconn),
			      _(palm_strerror(PConn_get_palmerrno(pconn))));
		}
	}

	SYNC_TRACE(5)
		fprintf(stderr, "===== Finished syncing\n");

	PConnClose(pconn);		/* Close the connection */

	return err;
}

struct Palm *
palm_Connect( void )
{
	listen_block *listen;
	PConnection *pconn;
	struct Palm *palm;
	int err;

	/* Get listen block */
	if ( (listen = find_listen_block(global_opts.listen_name)) == NULL )
	{
		Error(_("No port specified."));
		return NULL;
	}

	SYNC_TRACE(2)
		fprintf(stderr, "Opening device [%s]\n",
			listen->device);

	/* Set up a PConnection to the Palm */
	if ((pconn = new_PConnection(listen->device,
				     listen->listen_type,
				     listen->protocol,
				     PCONNFL_PROMPT |
				     (listen->flags &
				      LISTENFL_TRANSIENT ? PCONNFL_TRANSIENT :
				      0) |
				     (listen->flags &
				      LISTENFL_NOCHANGESPEED ? PCONNFL_NOCHANGESPEED :
				      0)
		     ))
	    == NULL)
	{
		Error(_("Can't open connection."));
		/* XXX - Say why */
		return NULL;
	}
	pconn->speed = listen->speed;

	/* Connect to the Palm */
	if ((err = Connect(pconn)) < 0)
	{
		Error(_("Can't connect to Palm."));
		/* XXX - Say why */
		PConnClose(pconn);
		return NULL;
	}

	/* Allocate a new Palm description */
	if ((palm = new_Palm(pconn)) == NULL)
	{
		Error(_("Can't allocate struct Palm."));
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
		return NULL;
	}

	return palm;
}

void
palm_Disconnect(struct Palm *palm, ubyte status)
{
	int err;

	/* Close the connection */

	SYNC_TRACE(3)
		fprintf(stderr, "Closing connection to Palm\n");

	if((err = Disconnect(palm_pconn(palm), status)) < 0)
		Error(_("Couldn't disconnect."));

	free_Palm(palm);
}

/* This is for Emacs's benefit:
 * Local Variables:	***
 * fill-column:	75	***
 * End:			***
 */
