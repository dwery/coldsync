/* PConnection.h
 *
 * Defines the PConnection abstraction, which embodies a connection
 * to a Palm device.
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: PConnection.h,v 1.31 2002-07-04 21:03:27 azummo Exp $
 */
#ifndef _PConnection_h_
#define _PConnection_h_

#include <termios.h>		/* For speed_t */
#include "palm.h"
#include "slp.h"
#include "padp.h"
#include "dlp.h"
#include <pconn/palm_errno.h>

/* XXX - which of these #includes actually belong here? */
#include <sys/types.h>			/* For select() */
#include <sys/time.h>			/* For select() */
#include <unistd.h>			/* For select() */
#include <string.h>			/* For bzero() for select() */

typedef enum { forReading = 0, forWriting = 1 } pconn_direction;

/* Types of listen blocks. These specify what kind of device (the file in
 * /dev) is like: serial, network, or what.
 */
typedef enum {
	LISTEN_NONE = -1,	/* No listen type. Used for errors */
	LISTEN_SERIAL = 0,	/* Listen on serial port */
	LISTEN_NET,		/* Listen on TCP/UDP port */
	LISTEN_USB,		/* USB for Handspring Visor */
	LISTEN_SPC		/* SPC over an existing file descriptor */
				/* XXX - Not implemented yet */
} pconn_listen_t;

/* Types of protocol stacks. These specify which protocols to use in
 * communicating with the cradle. These lie on top of the LISTEN_*
 * protocols.
 */
typedef enum {
	PCONN_STACK_NONE = -1,		/* No protocol. Used for errors */
	PCONN_STACK_DEFAULT = 0,	/* Use whatever the underlying line
					 * protocol thinks is appropriate.
					 */
	PCONN_STACK_FULL,		/* DLP -> PADP -> SLP -> whatever */
	PCONN_STACK_SIMPLE,		/* DLP -> netsync -> whatever
					 * This is used by the M50* Palms.
					 */
	PCONN_STACK_NET			/* DLP -> netsync -> whatever
					 * This is for NetSync. */
		/* SIMPLE and NET are very similar: the difference is that
		 * when they exchange ritual packets at the beginning of
		 * the sync, NET adds a header to the packets, and SIMPLE
		 * doesn't.
		 */
} pconn_proto_t;

/* Connection states
 */
typedef enum {
	PCONNSTAT_NONE = 0,		/* Initial status */
	PCONNSTAT_UP,			/* Connection established */
	PCONNSTAT_LOST,			/* Connection lost */
	PCONNSTAT_CLOSED		/* Connection closed */
} pconn_stat;	


/* Flags */
#define PCONNFL_TRANSIENT	0x0001	/* The device might not exist */
#define PCONNFL_PROMPT		0x0002	/* Prompt the user to press the
					 * HotSync button.
					 */
#define PCONNFL_MODEM		0x0004	/* This is a modem, don't change speeds */

/* Misc defines */
#define PCONN_NET_CONNECT_RETRIES 10	/* connect() retries */
#define PCONN_NET_CONNECT_DELAY	  1	/* Delay after each connect() */

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
	unsigned short flags;		/* Flags. See PCONNFL_*, above */

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
	pconn_proto_t protocol;	/* Protocol stack identifier. See
				 * PCONN_STACK_* in "pconn/PConnection.h".
				 * PConnection.c doesn't set this directly;
				 * this field is for the benefit of the
				 * various PConnection_* modules.
				 */

	void *io_private;	/* XXX - This is only used by the USB code.
				 * It'd be cleaner to either declare it as
				 * such, or just give it its own space in
				 * the struct, along with all the other
				 * protocols.
				 */

	pconn_stat status;	/* Connection status
				 */

	palmerrno_t palm_errno;	/* Latest error code
				 */

	int whosonfirst;	/* If 1 the connection has been locally initiated */

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
		ubyte last_xid;
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

extern PConnection *new_PConnection(char *fname,
				    const pconn_listen_t listenType,
				    const pconn_proto_t protocol,
				    unsigned short flags);
extern int PConnClose(PConnection *pconn);
extern int PConn_bind(PConnection *pconn,
		      const void *addr,
		      const int addrlen);

extern int PConn_read(struct PConnection *p,
                unsigned char *buf,
                int len);
extern int PConn_write(struct PConnection *p,
                unsigned const char *buf,
                const int len);
extern int PConn_connect(struct PConnection *p,
                  const void *addr, 
                  const int addrlen);
extern int PConn_accept(struct PConnection *p);
extern int PConn_drain(struct PConnection *p);
extern int PConn_close(struct PConnection *p);
extern int PConn_select(struct PConnection *p,
                 pconn_direction direction,
                 struct timeval *tvp);
extern palmerrno_t PConn_get_palmerrno(PConnection *p);
extern void PConn_set_palmerrno(PConnection *p, palmerrno_t errno);
extern void PConn_set_status(PConnection *p, pconn_stat status);
extern pconn_stat PConn_get_status(PConnection *p);
extern int PConn_isonline(PConnection *p);


extern int io_trace;
#define	IO_TRACE(n)	if (io_trace >= (n))
extern int net_trace;

#endif	/* _PConnection_h_ */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
