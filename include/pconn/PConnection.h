/* PConnection.h
 *
 * Defines the PConnection abstraction, which embodies a connection
 * to a Palm device.
 *
 *	Copyright (C) 1999-2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: PConnection.h,v 1.16.8.1 2001-07-29 07:26:05 arensb Exp $
 */
#ifndef _PConn_h_
#define _PConn_h_

#include "config.h"
#include <termios.h>		/* For speed_t */
#include "palm_types.h"
#include "slp.h"
#include "padp.h"
#include "dlp.h"

#include <sys/types.h>			/* For select() */
#if  HAVE_SYS_SELECT_H
#  include <sys/select.h>		/* To make select() work rationally
					 * under AIX */
#endif	/* HAVE_SYS_SELECT_H */
#include <sys/time.h>			/* For select() */
#include <unistd.h>			/* For select() */
#include <string.h>			/* For bzero() for select() */
#if HAVE_STRINGS_H
#  include <strings.h>			/* For bzero() under AIX */
#endif	/* HAVE_STRINGS_H */

typedef enum { forReading = 0, forWriting = 1 } pconn_direction;

/* Types of listen blocks */
#define LISTEN_NONE	0	/* Dunno if this will be useful */
#define LISTEN_SERIAL	1	/* Listen on serial port */
#define LISTEN_NET	2	/* Listen on TCP/UDP port (not
				 * implemented yet). */
#define LISTEN_USB	3	/* USB for Handspring Visor */
#define LISTEN_USB_M50x  4	/* USB for Palm m50x */

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
typedef struct PConnection
{
	/* Common part */
	int fd;				/* File descriptor */

	/* The following io_* fields are really virtual functions that
	 * allow you to choose between serial and USB I/O.
	 */
	int (*io_bind)(struct PConnection *p, const void *addr,
		       const int addrlen);
	int (*io_read)(struct PConnection *p, unsigned char *buf, int len);
	int (*io_write)(struct PConnection *p, unsigned const char *buf,
			const int len);
	int (*io_connect)(struct PConnection *p, const void *addr,
			  const int addrlen);
	int (*io_accept)(struct PConnection *p);
	int (*io_drain)(struct PConnection *p);
	int (*io_close)(struct PConnection *p);
	int (*io_select)(struct PConnection *p, pconn_direction direction,
			 struct timeval *tvp);

	long speed;		/* Speed at which to listen, for serial
				 * connections.
				 */

	void *io_private;	/* XXX - This is only used by the USB code.
				 * It'd be cleaner to either declare it as
				 * such, or just give it its own space in
				 * the struct, along with all the other
				 * protocols.
				 */

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

		/* 'read' and 'write' are methods, really: they point to
		 * functions that will read and write a DLP packet.
		 * XXX - 'len' should probably be udword in both cases, to
		 * support NetSync and PADP 2.x(?)
		 */
		int (*read)(struct PConnection *pconn,
			    const ubyte **buf,
			    uword *len);
		int (*write)(struct PConnection *pconn,
			     const ubyte *buf,
			     uword len);
	} dlp;

	/* NetSync protocol */
	struct {
		/* XXX - Does there need to be an address/protocol family
		 * field?
		 */
		ubyte xid;		/* Transaction ID */
		udword inbuf_len;	/* Current length of 'inbuf' */
		ubyte *inbuf;		/* Buffer to hold incoming packets */
	} net;

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
} PConnection;

extern PConnection *new_PConnection(char *fname, int listenType,
				    int prompt_for_hotsync);
extern int PConnClose(PConnection *pconn);
extern int PConn_bind(PConnection *pconn,
		      const void *addr,
		      const int addrlen);

extern int io_trace;
#define	IO_TRACE(n)	if (io_trace >= (n))
extern int net_trace;

#endif	/* _PConn_h_ */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
