/* PConnection.h
 *
 * Defines the PConnection abstraction, which embodies a connection
 * to a P device.
 *
 * $Id: PConnection.h,v 1.1 1999-02-19 22:51:54 arensb Exp $
 */
#ifndef _PConn_h_
#define _PConn_h_

#include <termios.h>		/* For speed_t */
#include "palm/palm_types.h"
#include "slp.h"
#include "padp.h"

/* PConnection
 * This struct is an opaque type that contains all of the state about
 * a connection to a given Palm device at a given time.
 * Programs should never use the PConnection type directly.
 * Instead, everything should use the PConnHandle type, defined
 * below.
 */
/* XXX - It'd be way cool to turn this into an honest-to-God socket */
struct PConnection
{
	/* Common part */
	struct PConnection *next;	/* Next PConnection on linked
					 * list */
	int fd;				/* File descriptor */

	/* Protocol-dependent parts */

	/* Packet Assembly/Disassembly Protocol (PADP) */
	struct {
		ubyte xid;	/* Transaction ID. PADP sets this, and
				 * SLP reads it when sending out a
				 * packet.
				 * This violates encapsulation, but I
				 * can't think of a better way to do
				 * it.
				 */
		int read_timeout;
				/* How long to wait (in 1/10ths of a
				 * second) for a PADP packet to come
				 * in.
				 */
	} padp;

	/* Serial Link Protocol (SLP) */
	struct {
		struct slp_addr local_addr;
		struct slp_addr remote_addr;

		/* The PConnection contains buffers for reading and
		 * writing SLP data. This is partly because there
		 * could conceivably be multiple connections (so
		 * static variables wouldn't do), and partly because
		 * there may be a need to un-read packets.
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
				 * packet that was received. PADP uses
				 * this for replies.
				 * This is a gross hack that violates
				 * encapsulation, but it's necessary
				 * since SLP and PADP are so closely
				 * tied.
				 */
	} slp;
};

/*  typedef struct PConnection *PConnHandle; */

extern struct PConnection *PConnLookup(int fd);
extern int new_PConnection(char *fname);
extern int PConnClose(int fd);
extern int PConn_bind(int fd, struct slp_addr *addr);
extern int PConnSetSpeed(int fd, speed_t speed);	/* XXX */

#endif	/* _PConn_h_ */
