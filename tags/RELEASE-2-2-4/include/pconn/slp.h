/* slp.h
 *
 * Types and definitions for Palm Serial Link Protocol (SLP).
 *
 *	Copyright (C) 1999-2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * Palm's documentation can be found at
 * http://www.palmos.com/dev/tech/docs/palmos/
 * under "Palm OS SDK Companion", section 9. The following is an abridged
 * version:
 *
 * Structure of a SLP packet:
 *	+------+------+------+------+------+
 *	| preamble           | dest | src  |
 *	+------+------+------+------+------+
 *	| type | size        | xid  | sum  |
 *	+------+------+------+------+------+
 *	| <size> bytes of user data        |
 *	+------+------+------+------+------+
 *	| CRC         |
 *	+------+------+
 *
 * Where:
 *	'preamble' is the mandatory 3-byte preamble to every SLP
 *	packet: the bytes 0xbe, 0xef, 0xed.
 *	'dest' and 'src' are the source and destination ports (Palm
 *	calls them "sockets". These serve to identify well-known
 *	services, much like TCP ports. See the SLP_PORT_* constants,
 *	below
 *	'type' is the type of packet. See the SLP_PKTTYPE_* constants,
 *	below.
 *	'xid' is the transaction ID of this packet. It isn't actually
 *	used by SLP, but is simply passed up to protocols higher up
 *	the stack. PADP, in particular, expects an ACK packet to have
 *	the same transaction ID as the packet to which it is a
 *	response.
 *	'sum' is a simple checksum of the SLP preamble and header
 *	(everything up to the checksum), excluding the checksum byte
 *	itself.
 *	'CRC' is a 16-bit CRC of the preamble, packet header, and user
 *	data.
 *
 * All values are unsigned.
 *
 * Error-correction:
 *	SLP is an unreliable protocol: if it receives a bad packet of
 *	any kind, its only response is to drop the packet on the
 *	floor. It is the job of protocols further up the stack
 *	(typically PADP) to ensure reliability.
 *	However, since SLP does include a checksum and CRC, if a
 *	packet is accepted, its contents are known to be good.
 *
 * $Id: slp.h,v 1.8 2001-09-07 23:35:56 arensb Exp $
 */
#ifndef _slp_h_
#define _slp_h_

#include "palm.h"

/* Predefined port numbers (Palm calls them Socket IDs) */
typedef enum {
	SLP_PORT_DEBUGGER	= 0,	/* Debugger port */
	SLP_PORT_CONSOLE	= 1,	/* Console port */
	SLP_PORT_REMOTEUI	= 2,	/* Remote UI port */
	SLP_PORT_DLP		= 3,	/* Desktop Link port */
	SLP_PORT_FIRSTDYNAMIC	= 4	/* First dynamic port */

	/* Ports 0x04-0xcf are reserved for dynamic assignment (a la RPC
	 * port mapper).
	 */
	/* Ports 0xd0-0xdf are reserved for testing (?) */
} slp_port;

/* Define the various packet types that SLP packets can encapsulate.
 * That is, the various protocols
 */
typedef enum {
	SLP_PKTTYPE_SYSTEM	= 0,	/* System packets */
			/* This includes Remote Debugger, Remote Console,
			 * and System Remote Procedure Call packets.
			 */
	SLP_PKTTYPE_UNUSED1	= 1,	/* Used to be Connection
					 * Manager packets */
	SLP_PKTTYPE_PAD		= 2,	/* PADP packets */
	SLP_PKTTYPE_LOOPBACK	= 3	/* Loopback test packets */
} slp_pkttype;

/* slp_addr
 * A structure defining an address that a socket can be bound to.
 * Currently, in the case of the SLP functions, this merely defines
 * which kinds of packets will be read: anything that does not match
 * the contents of a 'struct slp_addr' will be silently ignored.
 */
struct slp_addr
{
	ubyte protocol;		/* Which protocol to deal with. Should
				 * be one of the SLP_PKTTYPE_*
				 * constants. */
	ubyte port;		/* Port number. Should be one of the
				 * SLP_PORT_* constants.
				 */
};

struct slp_header
{
	ubyte dest;		/* Destination port */
	ubyte src;		/* Source port */
	ubyte type;		/* Type of packet (protocol) */
	uword size;		/* Size of packet */
	ubyte xid;		/* Transaction ID */
	ubyte checksum;		/* Preamble + header checksum */
};

#define SLP_PREAMBLE_LEN	3	/* Length of SLP preamble */
#define SLP_HEADER_LEN		10	/* Length of SLP header,
					 * including preamble */
#define SLP_CRC_LEN		2	/* Length of the CRC following
					 * each SLP packet */

#define SLP_INIT_INBUF_LEN	2*1024
				/* Initial length of the input buffer */
#define SLP_INIT_OUTBUF_LEN	SLP_INIT_INBUF_LEN
				/* Initial length of the output buffer */

struct PConnection;		/* Forward declaration */

extern int slp_init(struct PConnection *pconn);
extern int slp_tini(struct PConnection *pconn);
extern int slp_bind(struct PConnection *pconn, const struct slp_addr *addr);
extern int slp_read(struct PConnection *pconn, const ubyte **buf, uword *len);
extern int slp_write(struct PConnection *pconn, const ubyte *buf,
		     const uword len);

#endif	/* _slp_h_ */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
