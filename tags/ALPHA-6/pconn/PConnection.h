/* PConnection.h
 *
 * Defines the PConnection abstraction, which embodies a connection
 * to a P device.
 *
 * $Id: PConnection.h,v 1.4 1999-02-22 11:14:16 arensb Exp $
 */
#ifndef _PConn_h_
#define _PConn_h_

#include <termios.h>		/* For speed_t */
#include "palm/palm_types.h"
#include "slp.h"
#include "padp.h"
#include "dlp.h"

/* PConnection
 * This struct is an opaque type that contains all of the state about
 * a connection to a given Palm device at a given time.
 * Programs should never use the PConnection type directly.
 * Instead, everything should use the PConnHandle type, defined
 * below.
 * This bears a superficial resemblance to a socket, but I think it's
 * unlikely that this will ever become a real, honest-to-God socket type in
 * the kernel.
 */
struct PConnection
{
	/* Common part */
	int fd;				/* File descriptor */

	/* The HotSync protocol version number that the other end
	 * understands. This is determined by the CMP layer, and
	 * therefore ought to go in its part, but it's used by the
	 * layers above it, so it goes in the common part.
	 */
	ubyte ver_maj;		/* Major version */
	ubyte ver_min;		/* Minor version */

	/* Protocol-dependent parts */

	/* Desktop Link Protocol (DLP) */
	struct {
		int argv_len;	/* Current length of 'argv' */
		struct dlp_arg *argv;
				/* Holds arguments in a DLP response. This
				 * is actually an array that gets
				 * dynamically resized to hold however many
				 * arguments there are in the response.
				 */
		/* XXX - Should have something here for the log */
	} dlp;

	/* Packet Assembly/Disassembly Protocol (PADP) */
	struct {
		ubyte xid;	/* Transaction ID. PADP sets this, and SLP
				 * reads it when sending out a packet. This
				 * violates encapsulation, but I can't
				 * think of a better way to do it.
				 */
		int read_timeout;
				/* How long to wait (in 1/10ths of a
				 * second) for a PADP packet to come in.
				 */
		udword inbuf_len;	/* Current length of 'inbuf' */
		ubyte *inbuf;	/* A local buffer for holding
				 * multi-fragment messages. It grows
				 * dynamically as needed.
				 */
	} padp;

	/* Serial Link Protocol (SLP) */
	struct {
		struct slp_addr local_addr;
		struct slp_addr remote_addr;

		/* The PConnection contains buffers for reading and writing
		 * SLP data. This is partly because there could conceivably
		 * be multiple connections (so static variables wouldn't
		 * do), and partly because there may be a need to un-read
		 * packets.
		 */
		ubyte header_inbuf[SLP_HEADER_LEN];
				/* Buffer to hold incoming headers */
		ubyte *inbuf;	/* Input buffer. Dynamically allocated */
		long inbuf_len;	/* Current length of input buffer */
		ubyte crc_inbuf[SLP_CRC_LEN];
				/* Buffer to hold incoming CRCs */

		ubyte header_outbuf[SLP_HEADER_LEN];
				/* Buffer to hold outgoing headers */
		ubyte *outbuf;	/* Output buffer. Dynamically allocated */
		long outbuf_len;	/* Current length of output buffer */
		ubyte crc_outbuf[SLP_CRC_LEN];
				/* Buffer to hold outgoing CRCs */

		ubyte last_xid;	/* The transaction ID of the last SLP
				 * packet that was received. PADP uses this
				 * for replies.
				 * This is a gross hack that violates
				 * encapsulation, but it's necessary since
				 * SLP and PADP are so closely tied.
				 */
	} slp;
};

extern struct PConnection *new_PConnection(char *fname);
extern int PConnClose(struct PConnection *pconn);
extern int PConn_bind(struct PConnection *pconn, struct slp_addr *addr);
extern int PConnSetSpeed(struct PConnection *pconn, speed_t speed);	/* XXX */

#endif	/* _PConn_h_ */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */