/* cmp.c
 *
 * Implements Palm's Connection Management Protocol (CMP).
 *
 * $Id: cmp.c,v 1.3 1999-01-31 21:55:47 arensb Exp $
 */
#if 0
#include <stdio.h>
#include "cmp.h"
#include "padp.h"

int
cmp_send(PConnHandle ph,	/* Connection on which to send */
	 struct cmp_packet *packet)
				/* The information to send */
{
	int i;
	int err;
	static ubyte outbuf[10];	/* Output buffer */

	/* Dump the packet information, for debugging */
	fprintf(stderr, ">>> CMP: ");
	switch (packet->type) {
	    case CMP_TYPE_WAKEUP:
		fprintf(stderr, "wakeup packet");
		break;
	    case CMP_TYPE_INIT:
		fprintf(stderr, "init packet");
		break;
	    case CMP_TYPE_ABORT:
		fprintf(stderr, "abort packet");
		break;
	    default:
		fprintf(stderr, "Unknown packet type %d",
			packet->type);
		break;
	};
	fprintf(stderr, ", flags 0x%02x", packet->flags);
	fprintf(stderr, ", ver %d.%d",
		packet->ver_major,
		packet->ver_minor);
	fprintf(stderr, ", rate %ld\n", packet->rate);

	/* Build a packet to pass on to PADP */
	i = 0;
	outbuf[i] = packet->type;			i++;
	outbuf[i] = packet->flags;			i++;

	outbuf[i] = packet->ver_major;			i++;
	outbuf[i] = packet->ver_minor;			i++;
	outbuf[i] = 0;					i++;
	outbuf[i] = 0;					i++;

	outbuf[i] = (packet->rate >> 24) & 0xff;	i++;
	outbuf[i] = (packet->rate >> 16) & 0xff;	i++;
	outbuf[i] = (packet->rate >> 8) & 0xff;		i++;
	outbuf[i] = packet->rate & 0xff;		i++;

	/* Send it down to the PADP protocol. */
	err = padp_send(ph/*fd*/, outbuf, 10);
	if (err < 0)
		fprintf(stderr, "cmp_send: error in padp_send\n");

	return err;
}

int
cmp_recv(PConnHandle ph,	/* Connection on which to receive */
	 struct cmp_packet *packet)
				/* A packet to fill in */
{
	int err;
	static ubyte inbuf[10];

	/* Read the packet from PADP */
	err = padp_recv(ph/*fd*/, inbuf, 10);
	if (err < 0)
	{
		fprintf(stderr, "cmp_recv: Error in padp_recv\n");
		return err;
	}

	/* Dissect the packet into its component parts. */
	packet->type = inbuf[0];
	packet->flags = inbuf[1];
	packet->ver_major = inbuf[2];
	packet->ver_minor = inbuf[3];
	packet->rate =
		(inbuf[6] << 24) |
		(inbuf[7] << 16) |
		(inbuf[8] << 8)  |
		inbuf[9];
	/* XXX - Ought to remember the version number, so that the DLP
	 * stuff can avoid using functions that the Palm doesn't
	 * understand.
	 */

	/* Dump the packet information, for debugging */
	fprintf(stderr, "<<< CMP: ");
	switch (packet->type) {
	    case CMP_TYPE_WAKEUP:
		fprintf(stderr, "wakeup packet");
		break;
	    case CMP_TYPE_INIT:
		fprintf(stderr, "init packet");
		break;
	    case CMP_TYPE_ABORT:
		fprintf(stderr, "abort packet");
		break;
	    default:
		fprintf(stderr, "Unknown packet type %d",
			packet->type);
		break;
	};
	fprintf(stderr, ", flags 0x%02x", packet->flags);
	fprintf(stderr, ", ver %d.%d",
		packet->ver_major,
		packet->ver_minor);
	fprintf(stderr, ", rate %ld\n", packet->rate);

	return 0;
}
#endif	/* 0 */

#include <stdio.h>
#include "palm_types.h"
#include "padp.h"
#include "cmp.h"
#include "util.h"

#define CMP_DEBUG	1
#ifdef CMP_DEBUG
int cmp_debug = 0;

#define CMP_TRACE(level, format...)		\
	if (cmp_debug >= (level))		\
		fprintf(stderr, "CMP:" format)

#endif	/* CMP_DEBUG */

int cmp_errno;			/* Error code */

char *cmp_errlist[] = {
	"No error",		/* CMPERR_NOERR */
};

int
cmp_read(int fd,
	 struct cmp_packet *packet)
{
	int err;
	static ubyte inbuf[CMP_PACKET_LEN];
	ubyte *ptr;		/* Pointer into buffers */

	cmp_errno = CMPERR_NOERR;

	/* Read a PADP packet */
	err = padp_read(fd, inbuf, CMP_PACKET_LEN);
	/* XXX - Error-checking */

	/* Parse the packet */
	ptr = inbuf;
	packet->type = get_ubyte(&ptr);
	packet->flags = get_ubyte(&ptr);
	packet->ver_major = get_ubyte(&ptr);
	packet->ver_minor = get_ubyte(&ptr);
	packet->reserved = 0;	/* Just to be pedantic */
	ptr += 2;		/* Skip over the reserved word */
	packet->rate = get_udword(&ptr);

	CMP_TRACE(5, "Got a message: type %d, flags 0x%02x, v%d.%d, rate %ld\n",
		  packet->type,
		  packet->flags,
		  packet->ver_major,
		  packet->ver_minor,
		  packet->rate);

	return 0;
}

int
cmp_write(int fd,			/* File descriptor */
	  struct cmp_packet *packet)	/* The packet to send */
{
	int err;
	static ubyte outbuf[CMP_PACKET_LEN];
	ubyte *ptr;		/* Pointer into buffers */

	cmp_errno = CMPERR_NOERR;

	CMP_TRACE(5, "Sending type %d, flags 0x%02x, v%d.%d, rate %ld\n",
		  packet->type,
		  packet->flags,
		  packet->ver_major,
		  packet->ver_minor,
		  packet->rate);

	/* Build the outgoing packet from 'packet' */
	ptr = outbuf;
	put_ubyte(&ptr, packet->type);
	put_ubyte(&ptr, packet->flags);
	put_ubyte(&ptr, packet->ver_major);
	put_ubyte(&ptr, packet->ver_minor);
	put_uword(&ptr, 0);
	put_udword(&ptr, packet->rate);

	/* Send the packet as a PADP packet */
	err = padp_write(fd, outbuf, CMP_PACKET_LEN);
	/* XXX - Error-handling */

	return err;
}
