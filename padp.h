#ifndef _padp_h_
#define _padp_h_

#include "palm_types.h"
#include "slp.h"

#define PADP_DEFAULT_TIMEOUT		4000	/* Timeout, in ms */
#define PADP_DEFAULT_RETRIES		140	/* # retries */

#define PADP_FRAGMENT_TYPE_DATA		1	/* User data */
#define PADP_FRAGMENT_TYPE_ACK		2	/* Acknowledgement */
#define PADP_FRAGMENT_TYPE_NAK		3	/* No longer used */
#define PADP_FRAGMENT_TYPE_TICKLE	4	/* Prevent timeouts */
#define PADP_FRAGMENT_TYPE_ABORT	8	/* ? Abort */

#define PADP_FLAG_FIRST		0x80	/* This is the first fragment */
#define PADP_FLAG_LAST		0x40	/* This is the last fragment */
#define PADP_FLAG_ERRNOMEM	0x20	/* Error: receiver is out of memory */

#define PADP_MAX_PACKET_LEN	1024	/* Max total length of a packet */

struct padp_header
{
	ubyte type;		/* Fragment type (PADP_FRAGMENT_TYPE_*) */
	ubyte flags;		/* Flags */
	uword size;		/* Size of packet, or offset of fragment */
};

extern int padp_send(int fd, ubyte *outbuf, uword len);
extern int padp_recv(int fd, ubyte *packet, uword len);
extern int padp_send_ack(int fd,
			 struct padp_header *header,
			 struct slp_header *slp_head);
extern int padp_get_ack(int fd,
			struct padp_header *header,
			struct slp_header *slp_head);

#endif	/* _padp_h_ */
