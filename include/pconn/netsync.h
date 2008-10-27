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
 * 0xff could be a magic value used in the first packet of a connection.
 * The client is then supposed to match the XID used by the server.
 *
 * The data portion of the packet typically appears to be a DLP packet
 * (except for the beginning of the session).
 *
 * (Note: most all of this is conjecture: it could be that the <length>
 * field is really a short flags field followed by a length field.)
 *
 * $Id$
 */

#ifndef _netsync_h_
#define _netsync_h_

#include <sys/param.h>		/* For MAXHOSTNAMELEN */
#include "pconn.h"
#include "palm.h"

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

#define NETSYNC_WAIT_TIMEOUT	20	/* How long (in seconds) to wait
					 * for incoming data before timing
					 * out.
					 */

/* Protocol functions */
extern int netsync_init(PConnection *pconn);
extern int netsync_tini(PConnection *pconn);
extern int netsync_read(PConnection *pconn,
			const ubyte **buf,
			uword *len);
extern int netsync_read_method(PConnection *pconn,
			       const ubyte **buf,
			       uword *len,
			       const Bool no_header);
extern int netsync_write(PConnection *pconn,
			 const ubyte *buf,
			 const uword len);
extern int ritual_exch_client(PConnection *pconn);
extern int ritual_exch_server(PConnection *pconn);

#endif	/* _netsync_h_ */

/* This is for Emacs's benefit:
 * Local Variables:	***
 * fill-column:	75	***
 * End:			***
 */
