/* coldnamed.c
 *
 *	Copyright (C) 2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: coldnamed.c,v 2.1 2002-01-23 15:53:44 arensb Exp $
 */

#include "config.h"
#include <stdio.h>
#include <unistd.h>		/* For STDIN_FILENO */
#include <sys/types.h>		/* For getsockname() */
#include <sys/socket.h>		/* For getsockname() */
#include <netinet/in.h>		/* For struct sockaddr_in */
#include <syslog.h>		/* For syslog(). Duh. */

int
main(int argc, char *argv[])
{
	int err;
	struct sockaddr_in from;	/* Source socket */
	socklen_t fromlen;

	fromlen = sizeof(from);
	err = getsockname(STDIN_FILENO, (struct sockaddr *) &from, &fromlen);
	if (err < 0)
	{
		perror("getsockname");
		exit(1);
	}

	openlog("coldnamed", LOG_PID, LOG_DAEMON);
	syslog(LOG_ERR, "Got connection from somewhere.");
	/* XXX - Read the wakeup packet */
	/* XXX - Look up the appropriate response */
	/* XXX - Send back the response */

	sleep(10);

	exit(0);
}
