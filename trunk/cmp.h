/* cmp.h
 *
 * $Id: cmp.h,v 1.1 1999-01-22 18:13:12 arensb Exp $
 */
#ifndef _cmp_h_
#define _cmp_h_

/* XXX - Add in the new changes from <System/CMCommon.h> */

#include "palm_types.h"

#define CMP_TYPE_WAKEUP		1	/* Wakeup packet */
#define CMP_TYPE_INIT		2	/* Initiate communications */
#define CMP_TYPE_ABORT		3	/* Abort communications */

#define CMP_INITFLAG_CHANGERATE	0x80	/* Client wants to change baud rate */
#define CMP_INITFLAG_1MIN_TO	0x40	/* Set Pilot's receive timeout
					 * to 1 minute */
#define CMP_INITFLAG_2MIN_TO	0x20	/* Set Pilot's receive timeout
					 * to 2 minutes */
#define CMP_ABORTFLAG_VERSION	0x80	/* Abort: version mismatch */

/* cmp_packet
 * Structure of a CMP packet. Since they all have the same header and
 * body structure, the two are rolled into one.
 */
struct cmp_packet
{
	/* Header */
	ubyte type;		/* Packet type */

	/* Body */
	ubyte flags;		/* Flags */
	ubyte ver_major;	/* Major version of protocol */
	ubyte ver_minor;	/* Minor version of protocol */
	uword reserved;		/* Reserved. Must always be 0 */
	udword rate;		/* How fast to communicate (bps) */
};

extern int cmp_send(int fd, struct cmp_packet *packet);
extern int cmp_recv(int fd, struct cmp_packet *ret);

#endif	/* _cmp_h_ */
