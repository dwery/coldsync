/* coldnamed.c
 *
 *	Copyright (C) 2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: coldnamed.c,v 1.1 2002-03-19 12:00:10 arensb Exp $
 */

#include "config.h"
#include <stdio.h>
#include <unistd.h>		/* For STDIN_FILENO, getopt() */
#include <stdlib.h>		/* For atoi() */
#include <sys/types.h>		/* For getsockname() */
#include <sys/socket.h>		/* For getsockname() */
#include <netinet/in.h>		/* For struct sockaddr_in */
#include <sys/param.h>		/* For ntohl() and friends */
#include <syslog.h>		/* For syslog(). Duh. */
#include <errno.h>		/* For errno. Duh. */
#if STDC_HEADERS
#  include <string.h>		/* For strerror() */
#endif

#if HAVE_LIBINTL_H
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL_H */

#include "pconn/netsync.h"

/* Declarations of everything related to getopt(), for those OSes that
 * don't have it already (Windows NT). Note that not all of these are used.
 */
extern int getopt(int argc, char * const *argv, const char *optstring);
extern char *optarg;
extern int optind;
extern int optopt;
extern int opterr;
extern int optreset;

void usage(int argc, char *argv[]);
void print_version(void);

int named_trace = 0;			/* Debugging level */

#define NAMED_TRACE(n)	if (named_trace >= (n))

int
main(int argc, char *argv[])
{
	int err;
	int arg;			/* Current option */
	int oldoptind;			/* Previous value of 'optind', to
					 * allow us to figure out exactly
					 * which argument was bogus, and
					 * thereby print descriptive error
					 * messages. */
	struct sockaddr_in cliaddr;	/* Client's address */
	socklen_t cliaddr_len;		/* Length of client's address */
	int len;			/* Datagram length */
	unsigned char buf[1024];	/* XXX - Pick a better size */
	struct netsync_wakeup wakeup;	/* Wakeup packet sent by client */
	struct netsync_wakeup wakeup_ack;
					/* Wakeup acknowledgement */

	/* Parse command-line arguments */
	opterr = 0;			/* Don't want getopt() writing to
					 * stderr */
	oldoptind = optind;		/* Initialize "last argument"
					 * index.
					 */
	while ((arg = getopt(argc, argv, ":hVa:d:")) != -1)
	{
		switch (arg)
		{
		    case 'h':	/* -h: Print usage message and exit */
			usage(argc, argv);
			exit(0);

		    case 'V':	/* -V: Print version number and exit */
			print_version();
			exit(0);

		    case 'a':	/* -a <addr>: Return <addr> to callers */
			/* XXX - Not implemented yet */
			NAMED_TRACE(1)
				fprintf(stderr, "addr: [%s]\n", optarg);
			break;

		    case 'd':	/* -d <level>: Set debugging level */
			named_trace = atoi(optarg);
			break;

		    case ':':	/* An argument required an option, but none
				 * was given (e.g., "-u" instead of "-u
				 * daemon").
				 */
			fprintf(stderr,
				_("Missing option argument after \"%s\".\n"),
				argv[oldoptind]);
			usage(argc, argv);
			exit(1);

		    default:	/* Unknown option */
			fprintf(stderr, _("Unrecognized option: \"%s\".\n"),
				argv[oldoptind]);
			usage(argc, argv);
			exit(1);
		}

		oldoptind = optind;	/* Update for next iteration */
	}

	openlog("coldnamed", LOG_PID, LOG_DAEMON);
			/* If we ever want to log where the connection came
			 * from, it'll probably be useful to use
			 * getsockname() to get the remote end's address.
			 */
syslog(LOG_DEBUG, "Got connection from somewhere.");

	/* Read the wakeup packet */
	len = recvfrom(STDIN_FILENO, (char *) buf, sizeof(buf), 0,
		       (struct sockaddr *) &cliaddr,
		       &cliaddr_len);
	if (len < 0)
	{
		perror("recvfrom");
			/* The perror() is mainly for the benefit of people
			 * who run this program from the command line.
			 */
		syslog(LOG_ERR, "recvfrom: %s", strerror(errno));
		exit(1);
	}
syslog(LOG_DEBUG, "recvfrom len == %d", len);

	/* Parse the received packet. */
	wakeup.magic	= ntohs(* (short *) buf);
	wakeup.type	= buf[2];
	wakeup.unknown	= buf[3];
	wakeup.hostid	= ntohl(* (u_long *) (buf+4));
	wakeup.netmask	= ntohl(* (u_long *) (buf+8));
	strncpy(wakeup.hostname, (char *) buf+12, DLPCMD_MAXHOSTNAMELEN-1);
	wakeup.hostname[DLPCMD_MAXHOSTNAMELEN-1] = '\0';
	/* XXX - Make sure packet starts with appropriate magic. */
syslog(LOG_DEBUG, "magic 0x%04x, type %d, unknown %d, hostid 0x%08lx, netmask 0x%08lx, hostname \"%s\"",
       wakeup.magic, wakeup.type, wakeup.unknown,
       wakeup.hostid, wakeup.netmask, wakeup.hostname);

#if 0
	/* XXX - Look up the appropriate response */
	wakeup_ack.magic	= NETSYNC_WAKEUP_MAGIC;
	wakeup_ack.type		= 2;	/* Wakeup ACK */
					/* XXX - Make this a symbolic
					 * constant */
	wakeup_ack.unknown	= wakeup.unknown;
	wakeup_ack.hostid	= wakeup.hostid;
	wakeup_ack.netmask	= wakeup.netmask;
	strncpy(wakeup_ack.hostname, wakeup.hostname, DLPCMD_MAXHOSTNAMELEN-1);
syslog(LOG_DEBUG, "About to send back magic 0x%04x, type %d, unknown %d, hostid 0x%08lx, netmask 0x%08lx, hostname \"%s\"",
       wakeup_ack.magic, wakeup_ack.type, wakeup_ack.unknown,
       wakeup_ack.hostid, wakeup_ack.netmask, wakeup_ack.hostname);

	/* XXX - Send back the response */
	* (u_short *) buf	= htons(wakeup_ack.magic);
	buf[2]			= wakeup_ack.type;
	buf[3]			= wakeup_ack.unknown;
	* (u_long *) (buf+4)	= htonl(wakeup_ack.hostid);
	* (u_long *) (buf+8)	= htonl(wakeup_ack.netmask);
	strncpy((char *) buf+12, wakeup_ack.hostname, DLPCMD_MAXHOSTNAMELEN-1);
#endif	/* 0 */
syslog(LOG_DEBUG, "About to sendto(%d)", 12+strlen(wakeup_ack.hostname)+1);

/*	err = sendto(STDIN_FILENO, (const void *) buf, 12+strlen(wakeup_ack.hostname)+1, 0,
		     (struct sockaddr *) &cliaddr,
		     cliaddr_len);*/
	err = send(STDIN_FILENO, 
		   buf,
		   12+strlen(wakeup_ack.hostname)+1,
		   0);
syslog(LOG_DEBUG, "Sent datagram back, err == %d", err);
	if (err < 0)
		syslog(LOG_ERR, "sendto: %d %s", errno, strerror(errno));

	sleep(1);		/* XXX - Not sure why this is here */

	exit(0);
}

void
usage(int argc, char *argv[])
{
	/* XXX - Redo this with an array of strings, like coldsync */
	printf(_("Usage: %s [options]\n"), argv[0]);
	printf(_("Options:\n"
		 "\t-h:\tPrint this usage message and exit.\n"
		 "\t-V:\tPrint version and exit.\n"
		 "\t-a <addr>:\tReturn <addr> to callers.\n"
		 "\t-d <level>:\tSet debugging level.\n"));
}

void
print_version(void)
{
	printf(_("coldnamed, part of %s version %s\n"),
	       PACKAGE, VERSION);
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
