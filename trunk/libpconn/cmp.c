/* cmp.c
 *
 * Implements Palm's Connection Management Protocol (CMP).
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: cmp.c,v 1.15 2002-04-27 18:36:31 azummo Exp $
 */
#include "config.h"
#include <stdio.h>
#include "palm.h"
#include <pconn/palm_errno.h>
#include <pconn/padp.h>
#include <pconn/cmp.h>
#include <pconn/util.h>

#if HAVE_LIBINTL_H
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL_H */

int cmp_trace = 0;		/* Debugging level for CMP */
#define CMP_TRACE(n)	if (cmp_trace >= (n))

int
cmp_read(PConnection *pconn,
	 struct cmp_packet *packet)
{
	int err;
	const ubyte *inbuf = NULL;	/* Input data (from PADP) */
	uword inlen;		/* Length of input data */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */

	pconn->palm_errno = PALMERR_NOERR;

	/* Read a PADP packet */
	err = padp_read(pconn, &inbuf, &inlen);
	if (err < 0)
	{
		CMP_TRACE(3)
			fprintf(stderr, "cmp_read: padp_read() returned %d\n",
				err);
		return err;	/* Error */
	}

	CMP_TRACE(7)
	{
		fprintf(stderr, "CMP: Received a packet:\n");
		debug_dump(stderr, "CMP <<<", inbuf, inlen);
	}

	/* Parse the packet */
	rptr = inbuf;
	packet->type = get_ubyte(&rptr);
	packet->flags = get_ubyte(&rptr);
	packet->ver_major = get_ubyte(&rptr);
	packet->ver_minor = get_ubyte(&rptr);
	packet->reserved = 0;	/* Just to be pedantic */
	rptr += 2;		/* Skip over the reserved word */
	packet->rate = get_udword(&rptr);

	CMP_TRACE(5)
		fprintf(stderr,
			"CMP: Got a message: type %d, flags 0x%02x, "
			"v%d.%d, rate %ld\n",
			packet->type,
			packet->flags,
			packet->ver_major,
			packet->ver_minor,
			packet->rate);

	return 0;
}

int
cmp_write(PConnection *pconn,			/* File descriptor */
	  const struct cmp_packet *packet)	/* The packet to send */
{
	int err;
	static ubyte outbuf[CMP_PACKET_LEN];
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	pconn->palm_errno = PALMERR_NOERR;

	CMP_TRACE(5)
		fprintf(stderr,
			"CMP: Sending type %d, flags 0x%02x, v%d.%d, "
			"rate %ld\n",
			packet->type,
			packet->flags,
			packet->ver_major,
			packet->ver_minor,
			packet->rate);

	/* Build the outgoing packet from 'packet' */
	wptr = outbuf;
	put_ubyte(&wptr, packet->type);
	put_ubyte(&wptr, packet->flags);
	put_ubyte(&wptr, packet->ver_major);
	put_ubyte(&wptr, packet->ver_minor);
	put_uword(&wptr, 0);
	put_udword(&wptr, packet->rate);

	/* Send the packet as a PADP packet */
	CMP_TRACE(7)
	{
		fprintf(stderr, "CMP: Sending a packet:\n");
		debug_dump(stderr, "CMP >>>", outbuf, CMP_PACKET_LEN);
	}

	err = padp_write(pconn, outbuf, CMP_PACKET_LEN);
		/* Error-handling: just inherit the error from padp_write() */

	return err;
}

/* cmp_accept
 * Negotiate the CMP part of establishing a connection with the Palm. 'bps'
 * gives the desktop's desired speed. 0 means "I don't care. Whatever the
 * Palm suggests".
 * Returns the speed in bps if successful, or ~0 in case of error.
 * This function is here, and not in PConnection_* because it's used for
 * both serial and USB connections.
 */
udword
cmp_accept(PConnection *pconn, udword bps)
{
	int err;
	struct cmp_packet cmpp;

	do {
		CMP_TRACE(5)
			fprintf(stderr, "===== Waiting for wakeup packet\n");

		err = cmp_read(pconn, &cmpp);
		if (err < 0)
		{
			if (PConn_get_palmerrno(pconn) == PALMERR_TIMEOUT)
				continue;
	
			fprintf(stderr, _("Error during cmp_read: (%d) %s.\n"),
				(int) PConn_get_palmerrno(pconn),
				_(palm_strerror(PConn_get_palmerrno(pconn))));
			return ~0;
		}
	} while (cmpp.type != (ubyte) CMP_TYPE_WAKEUP);

	CMP_TRACE(5)
		fprintf(stderr, "===== Got a wakeup packet\n");

	/* Compose a reply */
	cmpp.type = (ubyte) CMP_TYPE_INIT;
	cmpp.ver_major = CMP_VER_MAJOR;
	cmpp.ver_minor = CMP_VER_MINOR;
	if (bps != 0)
		cmpp.rate = bps;
	cmpp.flags = CMP_IFLAG_CHANGERATE;
		/* At this point, the protocol allows us to leave the
		 * CMP_IFLAG_CHANGERATE flag at 0, to indicate that the
		 * main sync should continue at whatever rate we're
		 * currently at (9600 bps). In practice, however, nobody's
		 * going to want to sync at 9600 bps, so always set the
		 * flag.
		 */

	CMP_TRACE(5)
		fprintf(stderr, "===== Sending INIT packet\n");
	err = cmp_write(pconn, &cmpp);
	if (err < 0)
		return ~0;

	CMP_TRACE(5)
		fprintf(stderr, "===== Finished sending INIT packet\n");

	CMP_TRACE(4)
		fprintf(stderr, "Initialized CMP, returning speed %ld\n",
			cmpp.rate);
	return cmpp.rate;
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
