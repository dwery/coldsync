/* cmp.h
 *
 * Types and definitions for Palm Connection Management Protocol
 * (CMP).
 *
 * Structure of a CMP packet:
 *	+------+------+------+------+------+------+
 *	| type |flags |verMaj|verMin| unused      |
 *	+------+------+------+------+------+------+
 *	| rate                      |
 *	+------+------+------+------+
 *
 * Where:
 *	'type' is the packet type. See the CMP_PKTTYPE_* constants,
 *	below.
 *	'flags' is a flags field.
 *	'verMaj' and 'verMin' are the major and minor version numbers
 *	of the connection protocol being used. The Pilot 5000 (and,
 *	presumably, older models) uses version 1.0. The PalmPilot uses
 *	version 1.1.
 *	'rate' is a communications rate, in bits/sec. This is
 *	negotiated between the server and client[1].
 *
 * The Palm initiates a HotSync by sending out a WAKEUP packet giving
 * its protocol version number and the maximum rate at which it is
 * willing to communicate. This is not expected to change in future
 * versions, for backward-compatibility.
 * The desktop then responds with an INIT packet, giving its own
 * protocol version and a rate. The rate may be 0, meaning "whatever
 * we're using right now" (initially, 9600 bps), or it may make a
 * counter-offer. In the latter case, the server must set the "rate
 * change" flag in the CMP INIT packet.
 *
 * [1] In normal usage, the client is the host that initiates the
 * dialog, and the server is the one that sits waiting for incoming
 * connections. This, intuitively enough, would make the desktop
 * machine the server, and the Palm the client. The Palm headers,
 * however, have this reversed.
 *
 * $Id: cmp.h,v 1.3 1999-06-24 02:45:43 arensb Exp $
 */
#ifndef _cmp_h_
#define _cmp_h_

/* CMP message types */
#define CMP_TYPE_WAKEUP		1	/* Wakeup packet */
#define CMP_TYPE_INIT		2	/* Initiate communications */
#define CMP_TYPE_ABORT		3	/* Abort communications */
#define CMP_TYPE_EXTENDED	4	/* For future command
					 * extensions */

/* CMP flags */
/* INIT flags */
#define CMP_IFLAG_CHANGERATE	0x80	/* Rate change request */
#define CMP_IFLAG_RCV1TO	0x40	/* Set receive timeout to 1 min.
					 * (v1.1) */
#define CMP_IFLAG_RCV2TO	0x40	/* Set receive timeout to 2 min.
					 * (v1.1) */
/* ABORT flags: reason for abort */
#define CMP_AFLAG_VERSION	0x80	/* Protocol version mismatch */

#define CMP_PACKET_LEN		10	/* Length of a CMP packet */

/* CMP error codes */
#define CMPERR_NOERR	0	/* No error */

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

extern int cmp_read(struct PConnection *pconn, struct cmp_packet *packet);
extern int cmp_write(struct PConnection *pconn,
		     const struct cmp_packet *packet);

#endif	/* _cmp_h_ */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
