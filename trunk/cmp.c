/* cmp.c
 *
 * Implements Palm's Connection Management Protocol (CMP).
 *
 * $Id: cmp.c,v 1.1 1999-01-22 18:13:12 arensb Exp $
 */
#include <stdio.h>
#include "cmp.h"
#include "padp.h"

int
cmp_send(int fd,		/* File descriptor on which to send */
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
	fprintf(stderr, ", rate %d\n", packet->rate);

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
	err = padp_send(fd, outbuf, 10);
	if (err < 0)
		fprintf(stderr, "cmp_send: error in padp_send\n");

	return err;
}

int
cmp_recv(int fd,		/* File descriptor on which to receive */
	 struct cmp_packet *packet)
				/* A packet to fill in */
{
	int err;
	static ubyte inbuf[10];

	/* Read the packet from PADP */
	err = padp_recv(fd, inbuf, 10);
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
	fprintf(stderr, ", rate %d\n", packet->rate);

	return 0;
}
