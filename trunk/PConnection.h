/* PConnection.h
 *
 * Defines the PConnection abstraction, which embodies a connection
 * to a P device.
 *
 * $Id: PConnection.h,v 1.2 1999-01-31 21:54:35 arensb Exp $
 */
#ifndef _PConn_h_
#define _PConn_h_

#include <termios.h>
/*  #include <sys/protosw.h> */
#include "palm_types.h"
#include "slp.h"

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
	} padp;

	/* Serial Link Protocol (SLP) */
	struct {
		struct slp_addr local_addr;
		struct slp_addr remote_addr;

		/* Convenience stuff for reading and writing. */
		struct slp_header header;	
		ubyte buf[SLP_MAX_PACKET_LEN];
		uword crc;

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
