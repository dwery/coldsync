/* padp.c
 *
 * Implementation of the Palm PADP (Packet Assembly/Disassembly
 * Protocol).
 *
 * $Id: padp.c,v 1.2 1999-01-31 22:11:02 arensb Exp $
 */
#if 0
/* XXX - Still need to implement all the funky stuff: timeouts,
 * retransmission, and the like.
 * Probably the easiest way to do timeouts is to set
 *	term.c_cc[VTIME] = 40;
 * in the termios structure.
 */
#include <stdio.h>
#include "palm_types.h"
#include "slp.h"
#include "padp.h"

static ubyte last_xid;

int
padp_send(PConnHandle ph, ubyte *outbuf, uword len)
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
	slp_head.dest = 3;
	slp_head.src = 3;
	slp_head.type = slpp_pad;
	slp_head.size = 4+len;	/* XXX - Shouldn't be necessary */
fprintf(stderr, "last_xid == %d\n", last_xid);
if (++last_xid == 0) last_xid++;
fprintf(stderr, "last_xid == %d\n", last_xid);
	slp_head.xid = last_xid/*0xff*/;

	/* And pass the whole thing down to SLP */
	err = slp_send(ph/*fd*/, padp_buf, 4+len, &slp_head);

	/* Get an acknowledgement */
	padp_get_ack(ph/*fd*/, &header, &slp_head);

	return err;
}

int
padp_recv(PConnHandle ph, ubyte *packet, uword len)
{
	int i;
	int err;
	static ubyte inbuf[4+PADP_MAX_PACKET_LEN];
				/* Input buffer, large enough to hold
				 * the header, plus the largest
				 * allowable data packet.
				 */
	static struct padp_header header;
	static struct slp_header slp_head;

	/* XXX - Should set an alarm, so PADP can time out */
	/* Ask SLP for the first chunk */
	err = slp_recv(ph/*fd*/, inbuf, len+4,
		       &slp_head);
last_xid = slp_head.xid;
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
slp_head.xid = last_xid;
	padp_send_ack(ph/*fd*/, &header, &slp_head);

	/* If there are any more chunks left, read them */

return err;
}

/* padp_send_ack
 * Send an acknowledgment for the packet whose header is 'header'
 */
int
padp_send_ack(PConnHandle ph,
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
slp_head->xid = last_xid;

fprintf(stderr, ">>  PADP: ACK\n");
	err = slp_send(ph/*fd*/, outbuf, 4, slp_head);
	if (err < 0)
	{
		fprintf(stderr, "padp_send_ack: error in slp_send\n");
		return err;
	}

	return 0; 
}

int
padp_get_ack(PConnHandle ph,
	     struct padp_header *header,
	     struct slp_header *slp_head)
{
	int err;
	static ubyte inbuf[4+PADP_MAX_PACKET_LEN];
					/* Input buffer; should just
					 * hold a PADP header. */
/*  	static struct slp_cb control; */
	static struct padp_header in_head;

	/* Read a SLP packet */
	err = slp_recv(ph/*fd*/, inbuf, 4+PADP_MAX_PACKET_LEN, slp_head);
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
#endif	/* 0 */

#include <stdio.h>
#include <sys/types.h>			/* For select() */
#include <sys/time.h>			/* For select() */
#include <unistd.h>			/* For select() */
#include <string.h>			/* For bzero() for select() */
#include "palm_types.h"
#include "slp.h"
#include "padp.h"
#include "util.h"
#include "PConnection.h"

#define PADP_DEBUG	1
#ifdef PADP_DEBUG
int padp_debug = 0;

#define PADP_TRACE(level, format...)		\
	if (padp_debug >= (level))		\
		fprintf(stderr, "PADP:" format)

#endif	/* PADP_DEBUG */

int padp_errno;			/* Error code */

char *padp_errlist[] = {
	"No error",		/* PADPERR_NOERR */
};

/* bump_xid

 * Pick a new transaction ID by incrementing the existing one, and
 * skipping over the reserved ones (AFAIK, 0xff and 0x00 are
 * reserved).
 */
static void
bump_xid(struct PConnection *pconn)
{
	pconn->padp.xid++;		/* Increment the current xid */
	if ((pconn->padp.xid == 0xff) ||/* Skip past the reserved ones */
	    (pconn->padp.xid == 0x00))
		pconn->padp.xid = 1;
}

int
padp_read(int fd,		/* File descriptor to read from */
	  ubyte *buf,		/* Buffer to put the packet in */
	  uword len)		/* Max # bytes to read */
{
	int err;
	struct PConnection *pconn;	/* The connection */
	struct padp_header header;
	static ubyte inbuf[PADP_HEADER_LEN+PADP_MAX_PACKET_LEN];
				/* A buffer big enough to hold the largest
				 * PADP fragment. */
				/* XXX - This may need to move out
				 * into the PConnection if there's
				 * ever a need to un-read a PADP
				 * packet (which there probably will
				 * be).
				 */
	static ubyte outbuf[PADP_HEADER_LEN];
				/* Output buffer, for sending
				 * acknowledgments */
	ubyte *ptr;		/* Pointer into buffers */

	padp_errno = PADPERR_NOERR;

	/* Get the PConnection */
	if ((pconn = PConnLookup(fd)) == NULL)
	{
		fprintf(stderr, "padp_read: can't find PConnection for %d\n",
			fd);
		padp_errno = PADPERR_BADFD;
		return -1;
	}

	/* Read an SLP packet */
	/* XXX - Multi-packet messages */
	/* XXX - Timeouts */
	err = slp_read(fd, inbuf, PADP_HEADER_LEN+len);
	/* XXX - Check the value of 'err' and deal with errors */

	/* Parse the header */
	ptr = inbuf;
	header.type = get_ubyte(&ptr);
	header.flags = get_ubyte(&ptr);
	header.size = get_uword(&ptr);

	PADP_TRACE(5, "Got PADP message: type %d, flags 0x%02x, size %d\n",
		   header.type,
		   header.flags,
		   header.size);

#if PADP_DEBUG
	/* Dump the body, for debugging */
	if (padp_debug >= 6)
		debug_dump("PADP <<<", inbuf+PADP_HEADER_LEN,
			   err-PADP_HEADER_LEN);
#endif	/* PADP_DEBUG */

	/* Send an ACK, if necessary */
	/* XXX - Find out when it's necessary. Tickle packets aren't
	 * acknowledged. Are they the only ones? */
	/* Construct an output packet header and put it in 'outbuf' */
	ptr = outbuf;
	put_ubyte(&ptr, PADP_FRAGTYPE_ACK);
	put_ubyte(&ptr, header.flags);
	put_uword(&ptr, header.size);
	/* Set the transaction ID that the SLP layer will use when
	 * sending the ACK packet. This is a kludge, but is pretty
	 * much required, since SLP and PADP are rather tightly
	 * interwoven.
	 */
	pconn->padp.xid = pconn->slp.last_xid;

	PADP_TRACE(5, "Sending ACK: type %d, flags 0x%02x, size %d, xid %d\n",
		   PADP_FRAGTYPE_ACK,
		   header.flags,
		   header.size,
		   pconn->padp.xid);

	/* Send the ACK as a SLP packet */
	err = slp_write(fd, outbuf, PADP_HEADER_LEN);
	/* XXX - Error-checking */

	/* Copy the received message to the caller's buffer */
	memcpy(buf, inbuf+PADP_HEADER_LEN, len);

	return err;
}

/* padp_write
 * Write the contents of 'buf', of length 'len' bytes, to the file
 * descriptor 'fd'.
 * Returns: XXX
 */
int
padp_write(int fd,		/* The file descriptor to write to */
	   ubyte *buf,		/* Data to write */
	   uword len)		/* Length of data */
{
	int err;
	struct PConnection *pconn;	/* The connection */
	static ubyte outbuf[PADP_HEADER_LEN+PADP_MAX_PACKET_LEN];
				/* Outgoing buffer */
	static ubyte ack_buf[PADP_HEADER_LEN];
				/* Incoming buffer, for ACK packet */
	struct padp_header ack_header;
				/* Parsed incoming ACK packet */
	ubyte *ptr;		/* Pointer into buffers */
	int attempt;		/* Send attempt number */
fd_set readfds;
struct timeval timeout;

	padp_errno = PADPERR_NOERR;

	/* Get the PConnection */
	if ((pconn = PConnLookup(fd)) == NULL)
	{
		fprintf(stderr, "padp_write: can't find PConnection for %d\n",
			fd);
		padp_errno = PADPERR_BADFD;
		return -1;
	}

	/* XXX - Retries & timeouts */
	/* XXX - Fragmentation of long messages */

	bump_xid(pconn);		/* Pick a new transaction ID */

	/* Construct a header in 'outbuf' */
	ptr = outbuf;
	put_ubyte(&ptr, PADP_FRAGTYPE_DATA);	/* type */
	put_ubyte(&ptr, PADP_FLAG_FIRST |	/* flags */
		        PADP_FLAG_LAST);
		/* XXX - These flags are bogus for multi-fragment
		 * packets. */
	put_uword(&ptr, len);			/* size */
	PADP_TRACE(5, "Sending type %d, flags 0x%02x, size %d\n",
		   PADP_FRAGTYPE_DATA,
		   PADP_FLAG_FIRST | PADP_FLAG_LAST,
		   len);

	/* Append the caller's data to 'outbuf' */
	memcpy(outbuf+PADP_HEADER_LEN, buf, len);

timeout.tv_sec = PADP_ACK_TIMEOUT;
timeout.tv_usec = 0L;
	for (attempt = 0; attempt < PADP_MAX_RETRIES; attempt++)
	{
		/* Send 'outbuf' as a SLP packet */
		err = slp_write(fd, outbuf, PADP_HEADER_LEN+len);
		/* XXX - Error-checking */

		/* Get an ACK */
fprintf(stderr, "padp_write: waiting for ACK\n");
FD_ZERO(&readfds);
FD_SET(pconn->fd, &readfds);
err = select(pconn->fd+1, &readfds, NULL, NULL, &timeout);
fprintf(stderr, "select() returned %d\n", err);
if (err == 0)
{
	fprintf(stderr, "Timeout. Attempting to resend\n");
	continue;
}
		err = slp_read(fd, ack_buf, PADP_HEADER_LEN);
fprintf(stderr, "slp_read returned %d\n", err);
		/* XXX - Error-checking */

		/* Parse the ACK packet */
		ptr = ack_buf;
		ack_header.type = get_ubyte(&ptr);
		ack_header.flags = get_ubyte(&ptr);
		ack_header.size = get_uword(&ptr);
		PADP_TRACE(5, "Got an ACK: type %d, flags 0x%02x, size %d\n",
			   ack_header.type,
			   ack_header.flags,
			   ack_header.size);
		/* XXX - This whole section needs a lot of work. Need
		 * timeouts, retries, etc.
		 * One problem with the above code is the fixed-sized
		 * buffer. This will be a problem if we send a data
		 * packet, and the Palm sends an ACK, but the ACK gets
		 * lost on the way. In this case, the next PADP packet
		 * we receive will be a data packet. This implies that
		 * our ACK was received normally, so we should act as
		 * if we got the ACK in the first place. This means
		 * that we need to un-read the packet that just came
		 * in, return sucess, and get on with life.
		 */

		break;		/* Successfully got an ACK */
	}
if (attempt >= PADP_MAX_RETRIES)
{
fprintf(stderr, "PADP: Reached retry limit. Abandoning.\n");
return -1;		/* XXX - Set error code */
}

	return 0;	/* XXX */
}
