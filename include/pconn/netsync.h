/* netsync.c
 *
 * Declarations and definitions for stuff related to network syncing.
 *
 *	Copyright (C) 2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * NetSync outline:
 *
 * The machine with the cradle is the client. It talks to the server host,
 * which will do the actual work of syncing.
 *
 * The client starts out by sending one or more wakeup packets to the
 * server, on UDP port 14237. These packets are of the form
 *	+------+------+------+------+
 *	|    magic    | type |   ?  |
 *	+------+------+------+------+
 *	| hostid                    |
 *	+------+------+------+------+
 *	| netmask                   |
 *	+------+------+------+------+
 *	| NUL-terminated hostname...
 *	+------+------+------+------+
 *
 * Where <magic> is the constant 0xfade, <type> appears to be the type of
 * request (1 == wakeup packet, 2 == ACK). <hostid> and <netmask> are the
 * IPv4 address and netmask of the host to sync with. <hostname> is the
 * name of the host. HotSync appears to use the address as authoritative,
 * and presumably sends the name along mainly for the server's benefit.
 *
 * The server then sends back a UDP datagram with the same information,
 * except that <type> is set to 2. The client will send up to 3 wakeup
 * requests before giving up.
 *
 * Once the server has acknowledged the wakeup packet (thereby accepting
 * the connection), it listens on TCP port 14238. At this point, data goes
 * back and forth in the following format:
 *
 *	+------+------+
 *	| cmd  | xid  |
 *	+------+------+------+------+
 *	| length                    |
 *	+------+------+------+------+
 *	| <length> bytes of data...
 *	+------+------+------+------+
 *
 * Where <cmd> always appears to be 1; <xid> is a transaction ID or
 * sequence number: it starts at 0xff, and is incremented with each command
 * that the server sends.
 * 'pilot-link' treats the xid the same way as for SLP: once it hits 0xfe,
 * it is incremented directly to 0x01. It remains to be seen whether this
 * is necessary.
 *
 * The data portion of the packet typically appears to be a DLP packet
 * (except for the beginning of the session).
 *
 * (Note: most all of this is conjecture: it could be that the <length>
 * field is really a short flags field followed by a length field.)
 *
 * $Id: netsync.h,v 1.2 2000-12-24 21:24:26 arensb Exp $
 */
#ifndef _netsync_h_
#define _netsync_h_

#include <sys/param.h>		/* For MAXHOSTNAMELEN */
#include "pconn.h"
#include "pconn/palm_types.h"

#define NETSYNC_WAKEUP_MAGIC	0xfade
#define NETSYNC_WAKEUP_PORT	14237	/* UDP port on which the client
					 * sends out the wakeup request.
					 */
#define NETSYNC_DATA_PORT	14238	/* TCP port on which the client and
					 * server exchange sync data.
					 */

/* struct netsync_wakeup
 * At the beginning of the NetSync process, the client sends one or more
 * UDP packets with this structure to the server.
 */
struct netsync_wakeup
{
	uword magic;		/* Magic value identifying a wakeup packet */
	ubyte type;		/* Packet type? */
	ubyte unknown;		/* Might be the length of something */
				/* XXX - Find out what this is */
	udword hostid;		/* Server's host ID. Normally, this should
				 * be its IP address, but presumably this
				 * might be any unique identifier.
				 */
	udword netmask;		/* Netmask of <hostid>. Typically
				 * 0xffffff00.
				 */
	char hostname[DLPCMD_MAXHOSTNAMELEN];
				/* Server host name */
};

struct netsync_header
{
	ubyte cmd;		/* Command type. Always 1, AFAICT. */
	ubyte xid;		/* Sequence number (transaction ID) */
	udword len;		/* Length of payload */
};

#define NETSYNC_HDR_LEN	6	/* Length of NetSync header */

/* Protocol functions */
extern int netsync_init(PConnection *pconn);
extern int netsync_tini(PConnection *pconn);
extern int netsync_read(PConnection *pconn,
			const ubyte **buf,
			uword *len);
extern int netsync_write(PConnection *pconn,
			 const ubyte *buf,
			 const uword len);

#endif	/* _netsync_h_ */

/* This is for Emacs's benefit:
 * Local Variables:	***
 * fill-column:	75	***
 * End:			***
 */
