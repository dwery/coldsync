#ifndef _slp_h_
#define _slp_h_

#include "palm_types.h"

/* Define the various packet types that SLP packets can encapsulate.
 * That is, the various protocols
 */
#define SLP_PACKET_TYPE_SYSTEM		0
#define SLP_PACKET_TYPE_PAD		2
#define SLP_PACKET_TYPE_LOOPBACK	3

#define SLP_MAX_BODY_LEN	0x10000	/* Max length of encapsulated
					 * body. This is an
					 * implementation limit, not a
					 * protocol limit. (Actually,
					 * this is one larger than the
					 * largest value that the size
					 * field in the SLP header can
					 * represent.)
					 */
#define SLP_MAX_PACKET_LEN	10+SLP_MAX_BODY_LEN+2
					/* Max total length of an SLP
					 * packet: 10 bytes for the
					 * preamble and header,
					 * followed by the body and a
					 * 2-byte CRC. */

struct slp_header
{
	ubyte dst;		/* Destination port */
	ubyte src;		/* Source port */
	ubyte type;		/* Type of packet (protocol) */
	uword size;		/* Size of packet */
	ubyte transID;		/* Transaction ID */
	ubyte checksum;		/* Preamble + header checksum */
};

/* slp_cb
 * SLP control block, for indicating which types of SLP packets
 * slp_recv() should read, and which it should ignore.
 */
/* XXX - Rather crude for now */
struct slp_cb
{
	ubyte type;		/* Protocol to listen to */
};

extern int slp_recv(int fd, ubyte *buf, uword len,	
		    struct slp_cb *control,
		    struct slp_header *header);
extern int slp_send(int fd, ubyte *buf, uword len,
		    struct slp_header *header);

#endif	/* _slp_h_ */
