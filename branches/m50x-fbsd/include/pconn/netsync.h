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
 * This packet encapsulation protocol is used for both 'NetSync' and
 * talking to the Palm m50x in its USB Palm cradle.
 *
 * Data goes back and forth in the following format:
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
 * $Id: netsync.h,v 1.2.8.1 2001-07-29 07:26:06 arensb Exp $
 */

#ifndef _netsync_h_
#define _netsync_h_

#include <sys/param.h>		/* For MAXHOSTNAMELEN */
#include "pconn.h"
#include "pconn/palm_types.h"

/*
 * Ritual statements
 * These packets are sent back and forth during the initial handshaking
 * phase. I don't know what they mean. The sequence is:
 * client sends UDP wakeup packet
 * server sends UDP wakeup ACK
 * client sends ritual response 1
 * server sends ritual statement 2
 * client sends ritual response 2
 * server sends ritual statement 3
 * client sends ritual response 3
 *
 * The comments are mostly conjecture and speculation.
 */
extern ubyte ritual_resp1[];
#define ritual_resp1_size 22
extern ubyte ritual_stmt2[];
#define ritual_stmt2_size 50
extern ubyte ritual_resp2[];
#define ritual_resp2_size 50
extern ubyte ritual_stmt3[];
#define ritual_stmt3_size 46
extern ubyte ritual_resp3[];
#define ritual_resp3_size 8

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
extern int netsync_read_method(PConnection *pconn,
			       const ubyte **buf,
			       uword *len,
			       int no_header);
extern int netsync_write(PConnection *pconn,
			 const ubyte *buf,
			 const uword len);

#endif	/* _netsync_h_ */

/* This is for Emacs's benefit:
 * Local Variables:	***
 * fill-column:	75	***
 * End:			***
 */
