/* netsync.c
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: netsync.c,v 2.3 2002-12-18 01:41:04 azummo Exp $
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
#include <time.h>		/* For ctime() */
#include <syslog.h>		/* For syslog() */
#include <pwd.h>		/* For getpwent() */

/* Include I18N-related stuff, if necessary */
#if HAVE_LIBINTL_H
#  include <locale.h>		/* For setlocale() and friends */
#  include <libintl.h>
#endif	/* HAVE_LIBINTL_H */

#include "pconn/pconn.h"
#include "cs_error.h"
#include "coldsync.h"
#include "pdb.h"
#include "conduit.h"
#include "parser.h"
#include "pref.h"
#include "palment.h"
#include "net_compat.h"
#include "symboltable.h"
#include "palmconn.h"

/* mkforw_addr
 * The pda block has forwarding turned on. Look at all the pertinent
 * information and figure out the address to which the connection needs to
 * be forwarded.
 * '*addr' and '*netmask' are filled in by mkforw_addr(). Upon successful
 * completion, they will be pointers to sockaddrs that can be passed to
 * various socket functions establish a connection with the remote host.
 *
 * Returns 0 if successful, or a negative value in case of error.
 */
static int
mkforw_addr(struct Palm *palm,
	    pda_block *pda,
	    struct sockaddr **sa,
	    socklen_t *sa_len)
{
	int err;
	const char *hostname;		/* Name (or address, as a string)
					 * of the host to which to forward
					 * the sync.
					 */
	struct hostent *hostent = NULL;	/* From gethostby*() */
	struct servent *service;	/* NetSync wakeup service entry */
	int wakeup_port;		/* NetSync wakeup port number */

	service = getservbyname("netsync-wakeup", "udp");
				/* Try to get the entry for
				 * "netsync-wakeup" from /etc/services */
	if (service == NULL)
		wakeup_port = htons(NETSYNC_WAKEUP_PORT);
	else
		wakeup_port = service->s_port;

	SYNC_TRACE(4)
	{
		if (service != NULL)
		{
			int i;

			fprintf(stderr, "Got entry for netsync-wakeup/udp:\n");
			fprintf(stderr, "\tname: \"%s\"\n", service->s_name);
			fprintf(stderr, "\taliases:\n");
			for (i = 0; service->s_aliases[i] != NULL; i++)
				fprintf(stderr, "\t\t\"%s\"\n",
					service->s_aliases[i]);
			fprintf(stderr, "\tport: %d\n",
				ntohs(service->s_port));
			fprintf(stderr, "\tprotocol: \"%s\"\n",
				service->s_proto);
		} else {
			fprintf(stderr, "No entry for netsync-wakeup/udp\n");
		}
	}

	/* Get the name of the host to forward the connection to.
	 * Try the name given on the "forward:" line in .coldsyncrc first.
	 * If there isn't one, ask the Palm for the address of its
	 * preferred server host. If that isn't set, try the hostname.
	 *
	 * XXX - Would it be better first to check whether the address
	 * resolves and, if that fails, try to use the hostname?
	 */
	hostname = pda->forward_host;
	if (hostname == NULL)
	{
		hostname = palm_netsync_hostaddr(palm);
		if ((hostname == NULL) && !palm_ok(palm))
			/* Something went wrong */
			return -1;
	}

	if (hostname == NULL)
	{
		hostname = palm_netsync_hostname(palm);
		if ((hostname == NULL) && !palm_ok(palm))
			/* Something went wrong */
			return -1;
	}

	SYNC_TRACE(3)
		fprintf(stderr, "forward hostname is [%s]\n",
			(hostname == NULL ? "(null)" : hostname));

#if HAVE_SOCKADDR6
	/* Try to look up the name as IPv6 hostname */
	if ((hostent = gethostbyname2(hostname, AF_INET6)) != NULL)
	{
		struct sockaddr_in6 *sa6;	/* Temporary */

		SYNC_TRACE(3)
			fprintf(stderr, "It's an IPv6 hostname\n");

		/* Allocate a new sockaddr */
		sa6 = (struct sockaddr_in6 *)
			malloc(sizeof(struct sockaddr_in6));
		if (sa6 == NULL)
			return -1;

		/* Fill in the new sockaddr */
		bzero((void *) sa6, sizeof(struct sockaddr_in6));
		sa6->sin6_family = AF_INET6;
		sa6->sin6_port = wakeup_port;
		memcpy(&sa6->sin6_addr,
		       hostent->h_addr_list[0],
		       sizeof(struct in6_addr));

		SYNC_TRACE(3)
		{
			char namebuf[128];

			debug_dump(stderr, "sa6",
				   (ubyte *) sa6,
				   sizeof(struct sockaddr_in));
			fprintf(stderr, "Returning address [%s]\n",
				inet_ntop(AF_INET6,
					  &(sa6->sin6_addr),
					  namebuf, 128));
		}

		*sa = (struct sockaddr *) sa6;
		*sa_len = sizeof(struct sockaddr_in6);
		return 0;		/* Success */
	}

	SYNC_TRACE(3)
		fprintf(stderr, "It's not an IPv6 hostname\n");
#endif	/* HAVE_SOCKADDR6 */

	/* Try to look up the name as IPv4 hostname */
	if ((hostent = gethostbyname2(hostname, AF_INET)) != NULL)
	{
		struct sockaddr_in *sa4;	/* Temporary */

		SYNC_TRACE(3)
			fprintf(stderr, "It's an IPv4 hostname\n");

		/* Allocate a new sockaddr */
		sa4 = (struct sockaddr_in *)
			malloc(sizeof(struct sockaddr_in));
		if (sa4 == NULL)
			return -1;

		/* Fill in the new sockaddr */
		bzero((void *) sa4, sizeof(struct sockaddr_in));
		sa4->sin_family = AF_INET;
		sa4->sin_port = wakeup_port;
		memcpy(&sa4->sin_addr,
		       hostent->h_addr_list[0],
		       sizeof(struct in_addr));

		SYNC_TRACE(3)
		{
			char namebuf[128];

			debug_dump(stderr, "sa4",
				   (ubyte *) sa4,
				   sizeof(struct sockaddr_in));
			fprintf(stderr, "Returning address [%s]\n",
				inet_ntop(AF_INET,
					  &(sa4->sin_addr),
					  namebuf, 128));
		}

		*sa = (struct sockaddr *) sa4;
		*sa_len = sizeof(struct sockaddr_in);
		return 0;		/* Success */
	}

	SYNC_TRACE(3)
		fprintf(stderr, "It's not an IPv4 hostname\n");

#if HAVE_SOCKADDR6
	/* Try to use the name as an IPv6 address */
	{
		struct in6_addr addr6;
		struct sockaddr_in6 *sa6;

		/* Try to convert the string to an IPv6 address */
		err = inet_pton(AF_INET6, hostname, &addr6);
		if (err < 0)
		{
			SYNC_TRACE(3)
				fprintf(stderr,
					"Error in inet_pton(AF_INET6)\n");

			Perror("inet_pton");
		} else if (err == 0)
		{
			SYNC_TRACE(3)
				fprintf(stderr,
					"It's not a valid IPv6 address\n");
		} else {
			/* Succeeded in converting to IPv6 address */
			SYNC_TRACE(3)
				fprintf(stderr, "It's an IPv6 address\n");

			/* Allocate a new sockaddr */
			sa6 = (struct sockaddr_in6 *)
				malloc(sizeof(struct sockaddr_in6));
			if (sa6 == NULL)
				return -1;

			/* Fill in the new sockaddr */
			bzero((void *) sa6, sizeof(struct sockaddr_in6));
			sa6->sin6_family = AF_INET6;
			sa6->sin6_port = wakeup_port;
			memcpy(&sa6->sin6_addr, &addr6,
			       sizeof(struct in6_addr));

			*sa = (struct sockaddr *) sa6;
			*sa_len = sizeof(struct sockaddr_in6);
			return 0;		/* Success */
		}
	}
#endif	/* HAVE_SOCKADDR6 */

	/* Try to use the name as an IPv4 address */
	{
		struct in_addr addr4;
		struct sockaddr_in *sa4;

		/* Try to convert the string to an IPv4 address */
		err = inet_pton(AF_INET, hostname, &addr4);
		if (err < 0)
		{
			SYNC_TRACE(3)
				fprintf(stderr,
					"Error in inet_pton(AF_INET)\n");

			Perror("inet_pton");
		} else if (err == 0)
		{
			SYNC_TRACE(3)
				fprintf(stderr,
					"It's not a valid IPv4 address\n");
		} else {
			/* Succeeded in converting to IPv4 address */
			SYNC_TRACE(3)
				fprintf(stderr, "It's an IPv4 address\n");

			/* Allocate a new sockaddr */
			sa4 = (struct sockaddr_in *)
				malloc(sizeof(struct sockaddr_in));
			if (sa4 == NULL)
				return -1;

			/* Fill in the new sockaddr */
			bzero((void *) sa4, sizeof(struct sockaddr_in));
			sa4->sin_family = AF_INET;
			sa4->sin_port = wakeup_port;
			memcpy(&sa4->sin_addr, &addr4,
			       sizeof(struct in_addr));

			*sa = (struct sockaddr *) sa4;
			*sa_len = sizeof(struct sockaddr_in);
			return 0;		/* Success */
		}
	}

	/* Nothing worked */
	/* XXX - In this case, perhaps it'd be best to have a fallback
	 * server. Presumably this should be defined in "coldsync.conf" or
	 * something, and should default to "localhost".
	 */
	SYNC_TRACE(3)
		fprintf(stderr, "Nothing worked. I give up.\n");

	return -1;
}

/* forward_netsync
 * Listen for packets from either pconn, and forward them to the other.
 */
int
forward_netsync(PConnection *local, PConnection *remote)
{
	int err = 0;
	int maxfd;
	fd_set in_fds;
	fd_set out_fds;
	const ubyte *inbuf;
	uword inlen;

	/* Get highest-numbered file descriptor, for select() */
	maxfd = local->fd;
	if (remote->fd > maxfd)
		maxfd = remote->fd;

	for (;;)
	{
		FD_ZERO(&in_fds);
		FD_SET(local->fd, &in_fds);
		FD_SET(remote->fd, &in_fds);
		FD_ZERO(&out_fds);
		FD_SET(local->fd, &out_fds);
		FD_SET(remote->fd, &out_fds);

		err = select(maxfd+1, &in_fds, /*&out_fds*/NULL, NULL, NULL);
		SYNC_TRACE(5)
			fprintf(stderr, "select() returned %d\n", err);

		if (FD_ISSET(local->fd, &in_fds))
		{
			err = (*local->dlp.read)(local, &inbuf, &inlen);
			if (err < 0)
			{
				Perror("read local");
				break;
			}
			SYNC_TRACE(5)
				fprintf(stderr,
					"Read %d-byte message from local. "
					"err == %d\n",
					inlen, err);

			err = (*remote->dlp.write)(remote, inbuf, inlen);
			if (err < 0)
			{
				Perror("write remote");
				break;
			}
			SYNC_TRACE(5)
				fprintf(stderr,
					"Wrote %d-byte message to remote. "
					"err == %d\n",
					inlen, err);
		}

		if (FD_ISSET(remote->fd, &in_fds))
		{
			err = (*remote->dlp.read)(remote, &inbuf, &inlen);
			if (err < 0)
			{
				Perror("read remote");
				break;
			}
			SYNC_TRACE(5)
				fprintf(stderr,
					"Read %d-byte message from remote. "
					"err == %d\n",
					inlen, err);

			err = (*local->dlp.write)(local, inbuf, inlen);
			if (err < 0)
			{
				Perror("write local");
				break;
			}
			SYNC_TRACE(5)
				fprintf(stderr,
					"Wrote %d-byte message to local. "
					"err == %d\n",
					inlen, err);
		}
	}

	return 0;
}

int
forward(pda_block *pda, struct Palm *palm )
{
	/* XXX - Need to figure out if remote address is really
	 * local, in which case we really need to continue doing a
	 * normal sync.
	 */

	struct sockaddr *sa;
	socklen_t sa_len;
	PConnection *pconn_forw;
	int err;

	SYNC_TRACE(2)
		fprintf(stderr,
			"I ought to forward this sync to \"%s\" "
			"(%s)\n",
			(pda->forward_host == NULL ? "<whatever>" :
			 pda->forward_host),
			(pda->forward_name == NULL ? "(null)" :
			 pda->forward_name));

	/* Get list of addresses corresponding to this host. We do
	 * this now and not during initialization because in this
	 * age of dynamic DNS and whatnot, you can't assume that a
	 * machine will have the same addresses all the time.
	 */
	/* XXX - OTOH, most machines don't change addresses that
	 * quickly. So perhaps it'd be better to call
	 * get_hostaddrs() earlier, in case there are errors. Then,
	 * if the desired address isn't found, rerun
	 * get_hostaddrs() and see if that address has magically
	 * appeared.
	 */
	if ((err = get_hostaddrs()) < 0)
	{
		Error(_("Can't get host addresses."));
		return -1;
	}

	err = mkforw_addr(palm, pda, &sa, &sa_len);
	if (err < 0)
	{
		Error(_("Can't resolve forwarding address."));
		return -1;
	}

	SYNC_TRACE(3)
	{
		char namebuf[128];

		fprintf(stderr, "Forwarding to host [%s]\n",
			inet_ntop(sa->sa_family,
				  &(((struct sockaddr_in *) sa)->sin_addr),
				  namebuf, 128));
	}

	/* XXX - Check whether *sa is local. If it is, just do a
	 * local sync, normally.
	 */

	/* XXX - Perhaps the rest of this block should be put in
	 * forward_netsync()?
	 */
	/* Create a new PConnection for talking to the remote host */
	if ((pconn_forw = new_PConnection(NULL,
					  LISTEN_NET,
					  PCONN_STACK_DEFAULT,
					  0))
	    == NULL)
	{
		Error(_("Can't create connection to forwarding "
			"host."));
		free(sa);
		palm_Disconnect(palm, DLPCMD_SYNCEND_CANCEL);
		return -1;
	}

	/* Establish a connection to the remote host */
	/* XXX - This hangs forever if the remote host doesn't send
	 * back a wakeup ACK.
	 * XXX - Also, should be able to handle the case of the
	 * Palm cancelling the sync while this handshaking is going
	 * on.
	 */
	err = PConn_connect(pconn_forw, sa, sa_len);
	if (err < 0)
	{
		Error(_("Can't establish connection with forwarding "
			"host."));
			free(sa);
		PConnClose(pconn_forw);
		return -1;
	}

	err = forward_netsync(palm_pconn(palm), pconn_forw);
	if (err < 0)
	{
		Error(_("Network sync forwarding failed."));
		free(sa);
		PConnClose(pconn_forw);
		return -1;
	}

	free(sa);
	PConnClose(pconn_forw);
	return 0;
}

/* This is for Emacs's benefit:
 * Local Variables:	***
 * fill-column:	75	***
 * End:			***
 */
