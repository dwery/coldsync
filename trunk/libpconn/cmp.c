/* cmp.c
 *
 * Implements Palm's Connection Management Protocol (CMP).
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: cmp.c,v 1.6 2000-12-24 21:24:33 arensb Exp $
 */
#include "config.h"
#include <stdio.h>
#include <pconn/palm_types.h>
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

	palm_errno = PALMERR_NOERR;

	/* Read a PADP packet */
	err = padp_read(pconn, &inbuf, &inlen);
	/* XXX - padp_read() might return 0 == end of file, e.g. if the
	 * device exists but doesn't return any useful data (e.g.,
	 * /dev/null).
	 */
	if (err < 0)
		return err;	/* Error */

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

	palm_errno = PALMERR_NOERR;

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
	/* XXX - Error-handling */

	return err;
}

/* cmp_accept
 * Negotiate the CMP part of establishing a connection with the Palm. 'bps'
 * gives the desktop's desired speed. 0 means "I don't care. Whatever the
 * Palm suggests".
 * Returns the speed in bps if successful, or 0 in case of error.
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
			if (palm_errno == PALMERR_TIMEOUT)
				continue;
			fprintf(stderr, _("Error during cmp_read: (%d) %s\n"),
				palm_errno,
				_(palm_errlist[palm_errno]));
			return -1;
		}
	} while (cmpp.type != CMP_TYPE_WAKEUP);

	CMP_TRACE(5)
		fprintf(stderr, "===== Got a wakeup packet\n");

	/* Compose a reply */
	cmpp.type = CMP_TYPE_INIT;
	cmpp.ver_major = CMP_VER_MAJOR;
	cmpp.ver_minor = CMP_VER_MINOR;
	if ((bps != 0) && (cmpp.rate != bps))
	{
		/* Caller has requested a specific rate */
		cmpp.rate = bps;
		cmpp.flags = CMP_IFLAG_CHANGERATE;
	}

	CMP_TRACE(5)
		fprintf(stderr, "===== Sending INIT packet\n");
	cmp_write(pconn, &cmpp);	/* XXX - Error-checking */

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
