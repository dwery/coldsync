/* padp.h
 *
 * Types and definitions for Palm Packet Assembly/Disassembly Protocol
 * (PADP).
 *
 * Structure of a PADP data packet:
 *	+------+------+
 *	| type |flags |
 *	+------+------+
 *	| size        |
 *      +------+------+
 *	| <size> bytes|
 *	| of data.    |
 *      +------+------+
 *
 * Structure of a PADP ACK packet:
 *	+------+------+
 *	| type |flags |
 *	+------+------+
 *	| size        |
 *      +------+------+
 *
 * Where:
 *	'type' is the type of packet. See the PADP_FRAGTYPE_*
 *	constants, below. Of particular interest here are the 'data'
 *	and 'ack' types.
 *	'flags' are flags for the packet. Of particular interest are
 *	the 'first' flag, which indicates that this packet is the
 *	first fragment of a message, and the 'last' flag, which
 *	indicates that this is the last fragment of a message (of
 *	course, both or neither may be set).
 *	'size' indicates either the size of the message (in the first
 *	fragment), or the offset of the fragment in the message (in
 *	subsequent packets).
 *
 * The purpose of PADP is two-fold: first, as its name indicates, it
 * can break up long packets into smaller fragments, as well as
 * reassemble them afterwards.
 * Secondly, it adds reliability (which SLP doesn't provide): each
 * data packet must be acknowledged with an ACK packet (tickle packets
 * are not acknowledged). The ACK packet should have the same 'flags'
 * and 'size' field as the data packet it is in response to[1], as
 * well as the same SLP transaction ID (see "slp.h"). An ACK packet
 * should not contain any accompanying data, even if the 'size' field
 * is nonzero.
 * If the receiver cannot send an ACK immediately, it should send a
 * tickle (keepalive) packet every so often, so the connection doesn't
 * time out.
 *
 * [1] exception: the ACK packet may set the "out of memory" flag, to
 * indicate that the receiving end can't allocate enough memory to
 * receive the entire packet.
 *
 * $Id: padp.h,v 1.2 1999-01-31 22:07:29 arensb Exp $
 */
#ifndef _padp_h_
#define _padp_h_

#if 0
#include "palm_types.h"
#include "slp.h"

#define PADP_DEFAULT_TIMEOUT		4000	/* Timeout, in ms */
#define PADP_DEFAULT_RETRIES		140	/* # retries */

#define PADP_MAX_PACKET_LEN	1024	/* Max total length of a packet */

struct padp_header
{
	ubyte type;		/* Fragment type (PADP_FRAGMENT_TYPE_*) */
	ubyte flags;		/* Flags */
	uword size;		/* Size of packet, or offset of fragment */
};

extern int padp_send(PConnHandle ph, ubyte *outbuf, uword len);
extern int padp_recv(PConnHandle ph, ubyte *packet, uword len);
extern int padp_send_ack(PConnHandle ph,
			 struct padp_header *header,
			 struct slp_header *slp_head);
extern int padp_get_ack(PConnHandle ph,
			struct padp_header *header,
			struct slp_header *slp_head);
#endif	/* 0 */

#include "palm_types.h"

/* PADP fragment types */
#define PADP_FRAGTYPE_DATA	1	/* User data */
#define PADP_FRAGTYPE_ACK	2	/* Acknowledgement */
#define PADP_FRAGTYPE_NAK	3	/* No longer used */
#define PADP_FRAGTYPE_TICKLE	4	/* Prevent timeouts */
#define PADP_FRAGTYPE_ABORT	8	/* ? Abort */

/* PADP header flags */
#define PADP_FLAG_LONGHDR	0x10	/* This is a long header */
#define PADP_FLAG_ERRNOMEM	0x20	/* Error: receiver is out of memory */
#define PADP_FLAG_LAST		0x40	/* This is the last fragment */
#define PADP_FLAG_FIRST		0x80	/* This is the first fragment */

#define PADP_HEADER_LEN		4	/* Length of a PADP header, in bytes */
#define PADP_MAX_PACKET_LEN	1024	/* Max. length of a single
					 * PADP packet (not including
					 * the PADP header). */
#define PADP_MAX_MESSAGE_LEN	64*1024	/* Max. length of a complete
					 * PADP message (multiple
					 * fragments. */
#define PADP_MAX_RETRIES	10	/* # of times to try sending a
					 * packet. */
#define PADP_ACK_TIMEOUT	2	/* # seconds to wait for an ACK */
#define PADP_WAIT_TIMEOUT	30	/* # seconds to wait for the
					 * next data packet. */
#define PADP_TICKLE_TIMEOUT	3	/* Time after which to send a
					 * tickle (in seconds) */

/* PADP error codes */
#define PADPERR_NOERR		0	/* No error */
#define PADPERR_BADFD		1	/* Bad file descriptor */

/* padp_header
 * The header of a PADP packet.
 *
 * XXX - the Palm includes indicate that at some point, there will be
 * a long header type, which supports longer packets than the current
 * 64Kb maximum. These should be supported.
 */
struct padp_header
{
	ubyte type;		/* Fragment type (PADP_FRAGTYPE_*) */
	ubyte flags;		/* Flags */
	uword size;		/* Size of packet, or offset of fragment */
};

extern int padp_errno;		/* Error code */
extern char *padp_errlist[];	/* Error messages */

extern int padp_read(int fd, ubyte *buf, uword len);
extern int padp_write(int fd, ubyte *buf, uword len);
extern int padp_unget(int fd);	/* XXX - Is this desirable? */

#endif	/* _padp_h_ */
