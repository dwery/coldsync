/* cmp.c
 *
 * Implements Palm's Connection Management Protocol (CMP).
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: cmp.c,v 1.3 2000-05-19 12:07:27 arensb Exp $
 */
#include "config.h"
#include <stdio.h>
#include <pconn/palm_types.h>
#include <pconn/palm_errno.h>
#include <pconn/padp.h>
#include <pconn/cmp.h>
#include <pconn/util.h>

int cmp_trace = 0;		/* Debugging level for CMP */
#define CMP_TRACE(n)	if (cmp_trace >= (n))

int
cmp_read(struct PConnection *pconn,
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
cmp_write(struct PConnection *pconn,			/* File descriptor */
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
	err = padp_write(pconn, outbuf, CMP_PACKET_LEN);
	/* XXX - Error-handling */

	return err;
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */