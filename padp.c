/* padp.c
 *
 * Implementation of the Palm PADP (Packet Assembly/Disassembly
 * Protocol).
 *
 * $Id: padp.c,v 1.1 1999-01-22 18:13:12 arensb Exp $
 */
/* XXX - Still need to implement all the funky stuff: timeouts,
 * retransmission, and the like.
 * Probably the easiest way to do timeouts is to set
 *	term.c_cc[VTIME] = 40;
 * in the termios structure.
 */
#include <stdio.h>
#include "slp.h"
#include "padp.h"

static ubyte last_xid;

int
padp_send(int fd, ubyte *outbuf, uword len)
{
	int err;
	static struct padp_header header;
	static ubyte padp_buf[4+PADP_MAX_PACKET_LEN];
	static struct slp_header slp_head;

	/* Sanity check: make sure the length is within allowable limits */
	if (len > PADP_MAX_PACKET_LEN)
	{
		fprintf(stderr, "padp_send: error: packet length (%d) "
			"exceeds maximum (%d)\n",
			len, PADP_MAX_PACKET_LEN);
		return -1;
	}

	header.type = PADP_FRAGMENT_TYPE_DATA;
	header.flags = PADP_FLAG_FIRST | PADP_FLAG_LAST;
				/* XXX - Need to check 'len' and see
				 * if this packet needs to be broken
				 * up into fragments. */
	header.size = len;

	/* Dump the header, for debugging */
	fprintf(stderr, ">>  PADP: type %d ", header.type);
	switch (header.type)
	{
	    case PADP_FRAGMENT_TYPE_DATA:
		fprintf(stderr, "(data)");
		break;
	    case PADP_FRAGMENT_TYPE_ACK:
		fprintf(stderr, "(ACK)");
		break;
	    case PADP_FRAGMENT_TYPE_TICKLE:
		fprintf(stderr, "(tickle)");
		break;
	    case PADP_FRAGMENT_TYPE_ABORT:
		fprintf(stderr, "(abort)");
		break;
	    default:
		fprintf(stderr, "(Unknown)");
		break;
	}
	fprintf(stderr, ",flags ");
	if (header.flags & PADP_FLAG_FIRST)
		fprintf(stderr, " FIRST");
	if (header.flags & PADP_FLAG_LAST)
		fprintf(stderr, " LAST");
	if (header.flags & PADP_FLAG_ERRNOMEM)
		fprintf(stderr, " ERRNOMEM");
	fprintf(stderr, " 0x%02x",
		header.flags & ~(PADP_FLAG_FIRST |
				 PADP_FLAG_LAST |
				 PADP_FLAG_ERRNOMEM));
	fprintf(stderr, ", size %d\n", header.size);

	/* Put the header in the outgoing buffer */
	padp_buf[0] = header.type;
	padp_buf[1] = header.flags;
	padp_buf[2] = (header.size >> 8) & 0xff;
	padp_buf[3] = header.size & 0xff;

	/* Now add the contents of the caller's buffer */
	memcpy(padp_buf+4, outbuf, len);

	/* Construct an SLP header */
	/* XXX - need some communication with receiver */
	slp_head.dst = 3;
	slp_head.src = 3;
	slp_head.type = SLP_PACKET_TYPE_PAD;
	slp_head.size = 4+len;	/* XXX - Shouldn't be necessary */
fprintf(stderr, "last_xid == %d\n", last_xid);
if (++last_xid == 0) last_xid++;
fprintf(stderr, "last_xid == %d\n", last_xid);
	slp_head.transID = last_xid/*0xff*/;

	/* And pass the whole thing down to SLP */
	err = slp_send(fd, padp_buf, 4+len, &slp_head);

	/* Get an acknowledgement */
	padp_get_ack(fd, &header, &slp_head);

	return err;
}

int
padp_recv(int fd, ubyte *packet, uword len)
{
	int i;
	int err;
	static ubyte inbuf[4+PADP_MAX_PACKET_LEN];
				/* Input buffer, large enough to hold
				 * the header, plus the largest
				 * allowable data packet.
				 */
	static struct padp_header header;
	static struct slp_cb control = {
		SLP_PACKET_TYPE_PAD,
	};
	static struct slp_header slp_head;

	/* XXX - Should set an alarm, so PADP can time out */
	/* Ask SLP for the first chunk */
	err = slp_recv(fd, inbuf, len+4,
		       &control,
		       &slp_head);
last_xid = slp_head.transID;
	if (err < 0)
	{
		fprintf(stderr, "padp_recv: error from slp_recv\n");
		return err;
	}

	/* Dissect the packet into header fields */
	header.type = inbuf[0];
	header.flags = inbuf[1];
	header.size = ((uword) inbuf[2] << 8) |
		inbuf[3];

	/* Print the contents of the packet */
	fprintf(stderr, "<<  PADP: type %d ", header.type);
	switch (header.type)
	{
	    case PADP_FRAGMENT_TYPE_DATA:
		fprintf(stderr, "(data)");
		break;
	    case PADP_FRAGMENT_TYPE_ACK:
		fprintf(stderr, "(ACK)");
		break;
	    case PADP_FRAGMENT_TYPE_TICKLE:
		fprintf(stderr, "(tickle)");
		break;
	    case PADP_FRAGMENT_TYPE_ABORT:
		fprintf(stderr, "(abort)");
		break;
	    default:
		fprintf(stderr, "(Unknown)");
		break;
	}
	fprintf(stderr, ",flags ");
	if (header.flags & PADP_FLAG_FIRST)
		fprintf(stderr, " FIRST");
	if (header.flags & PADP_FLAG_LAST)
		fprintf(stderr, " LAST");
	if (header.flags & PADP_FLAG_ERRNOMEM)
		fprintf(stderr, " ERRNOMEM");
	fprintf(stderr, " 0x%02x",
		header.flags & ~(PADP_FLAG_FIRST |
				 PADP_FLAG_LAST |
				 PADP_FLAG_ERRNOMEM));
	fprintf(stderr, ", size %d\n", header.size);

	/* Dump the body, for debugging */
	if (header.size > 0)
	{
		for (i = 0; i < header.size; i++)
		{
			fprintf(stderr, "%02x ", inbuf[i+4]);
			if ((i % 16) == 15)
				fprintf(stderr, "\n");
		}
		if ((i % 16) != 15)
			fprintf(stderr, "\n");
	}

	/* Copy the data to the caller's buffer */
	if (len < header.size)
	{
		fprintf(stderr, "padp_recv: buffer not big enough: can't fit %d bytes into %d\n",
			header.size, len);
		return -1;		/* XXX - Acknowledge first? */
	}
	memcpy(packet, inbuf+4, header.size);

	/* XXX - Acknowledge the first chunk */
/*  fprintf(stderr, "sleep(1)\n"); */
/*  sleep(1); */
/*  if (++last_xid == 0) */
/*  ++last_xid; */
slp_head.transID = last_xid;
	padp_send_ack(fd, &header, &slp_head);

	/* If there are any more chunks left, read them */

return err;
}

/* padp_send_ack
 * Send an acknowledgment for the packet whose header is 'header'
 */
int
padp_send_ack(int fd,
	      struct padp_header *header,
	      struct slp_header *slp_head)
{
	int err;
	static ubyte outbuf[4];		/* Output buffer */

	/* Construct a buffer from the header */
	outbuf[0] = PADP_FRAGMENT_TYPE_ACK;
	outbuf[1] = header->flags;
	outbuf[2] = (header->size >> 8) & 0xff;
	outbuf[3] = header->size & 0xff;
slp_head->transID = last_xid;

fprintf(stderr, ">>  PADP: ACK\n");
	err = slp_send(fd, outbuf, 4, slp_head);
	if (err < 0)
	{
		fprintf(stderr, "padp_send_ack: error in slp_send\n");
		return err;
	}

	return 0; 
}

int
padp_get_ack(int fd,
	     struct padp_header *header,
	     struct slp_header *slp_head)
{
	int err;
	static ubyte inbuf[4+PADP_MAX_PACKET_LEN];
					/* Input buffer; should just
					 * hold a PADP header. */
	static struct slp_cb control;
	static struct padp_header in_head;

	control.type = SLP_PACKET_TYPE_PAD;

	/* Read a SLP packet */
	err = slp_recv(fd, inbuf, 4+PADP_MAX_PACKET_LEN, &control, slp_head);
	if (err < 0)
	{
		fprintf(stderr, "padp_get_ack: error in slp_recv\n");
		return err;
	}

	/* Dissect the received message */
	in_head.type = inbuf[0];
	in_head.flags = inbuf[1];
	in_head.size = ((uword) inbuf[2] << 8) | inbuf[3];

	/* Print the contents of the packet */
	fprintf(stderr, "<<  PADP: type %d ", in_head.type);
	switch (in_head.type)
	{
	    case PADP_FRAGMENT_TYPE_DATA:
		fprintf(stderr, "(data)");
		break;
	    case PADP_FRAGMENT_TYPE_ACK:
		fprintf(stderr, "(ACK)");
		break;
	    case PADP_FRAGMENT_TYPE_TICKLE:
		fprintf(stderr, "(tickle)");
		break;
	    case PADP_FRAGMENT_TYPE_ABORT:
		fprintf(stderr, "(abort)");
		break;
	    default:
		fprintf(stderr, "(Unknown)");
		break;
	}
	fprintf(stderr, ",flags ");
	if (in_head.flags & PADP_FLAG_FIRST)
		fprintf(stderr, " FIRST");
	if (in_head.flags & PADP_FLAG_LAST)
		fprintf(stderr, " LAST");
	if (in_head.flags & PADP_FLAG_ERRNOMEM)
		fprintf(stderr, " ERRNOMEM");
	fprintf(stderr, " 0x%02x",
		in_head.flags & ~(PADP_FLAG_FIRST |
				 PADP_FLAG_LAST |
				 PADP_FLAG_ERRNOMEM));
	fprintf(stderr, ", size %d\n", in_head.size);

	/* Make sure it's a proper ACK */
	if (in_head.type != PADP_FRAGMENT_TYPE_ACK)
	{
		fprintf(stderr, "padp_get_ack: Error: this isn't an ACK\n");
		return -1;
	}

	fprintf(stderr, "<<  PADP: ACK\n");

	return 0;
}
