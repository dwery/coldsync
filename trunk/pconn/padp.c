/* padp.c
 *
 * Implementation of the Palm PADP (Packet Assembly/Disassembly
 * Protocol).
 *
 * Note on nomenclature: the term 'packet' is used somewhat loosely
 * throughout this file, and can mean "data passed from a protocol
 * further up the stack" or "data sent down to a protocol further down
 * the stack (SLP)", or something else, depending on context.
 *
 * $Id: padp.c,v 1.2 1999-02-21 08:15:03 arensb Exp $
 */
#include <stdio.h>
#include <sys/types.h>			/* For select() */
#include <sys/time.h>			/* For select() */
#include <unistd.h>			/* For select() */
#include <string.h>			/* For bzero() for select() */
#include "palm/palm_types.h"
#include "palm_errno.h"
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

/* padp_init
 * Initialize the PADP part of a PConnection.
 */
int
padp_init(struct PConnection *pconn)
{
	pconn->padp.read_timeout = PADP_WAIT_TIMEOUT;
				/* How long to wait for a PADP packet
				 * to arrive */
	return 0;
}

/* padp_tini
 * Clean up the PADP part of a PConnection that's being closed.
 */
int
padp_tini(struct PConnection *pconn)
{
	/* Nothing to do, really */
	return 0;
}

/* padp_read
 * Read a PADP packet from the given file descriptor. A pointer to the
 * packet data (without the PADP header) is put in `*buf'. The length
 * of the data (not counting the PADP header) is put in `*len'.
 *
 * If successful, returns a non-negative value. In case of error,
 * returns a negative value and sets `palm_errno' to indicate the
 * error.
 */
int
padp_read(int fd,		/* File descriptor to read from */
	  const ubyte **buf,	/* Buffer to put the packet in */
	  uword *len)		/* Length of received message */
{
	int err;
	struct PConnection *pconn;	/* The connection */
	struct padp_header header;	/* Header of incoming packet */
	static ubyte outbuf[PADP_HEADER_LEN];
				/* Output buffer, for sending
				 * acknowledgments */
	const ubyte *inbuf;	/* Incoming data, from SLP layer */
	uword inlen;		/* Length of incoming data */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */
	fd_set readfds;		/* Set of readable file descriptors
				 * (for select()) */
	struct timeval timeout;	/* Read timeout, for select() */

	palm_errno = PALMERR_NOERR;

	/* Get the PConnection */
	if ((pconn = PConnLookup(fd)) == NULL)
	{
		fprintf(stderr, "padp_read: can't find PConnection for %d\n",
			fd);
		palm_errno = PALMERR_BADF;
		return -1;
	}

	/* XXX - Multi-packet messages */

  retry:		/* It can take several attempts to read a packet,
			 * if the Palm sends tickles.
			 */
	/* Use select() to wait for the file descriptor to be readable. If
	 * nothing comes in in time, conclude that the connection is dead,
	 * and time out.
	 */
	timeout.tv_sec = pconn->padp.read_timeout / 10;
	timeout.tv_usec = 0L;		/* Set the timeout */

	FD_ZERO(&readfds);		/* Set of file descriptors to
					 * listen to */
	FD_SET(pconn->fd, &readfds);
	err = select(pconn->fd+1, &readfds, NULL, NULL, &timeout);
					/* Wait for 'pconn->fd' to become
					 * readable, or time out.
					 */
	if (err == 0)
	{
		/* select() timed out */
		palm_errno = PALMERR_TIMEOUT;
		return -1;
	}

	/* Read an SLP packet */
	err = slp_read(fd, &inbuf, &inlen);
	if (err == 0)
		return 0;		/* End of file: no data read */
	if (err < 0)
		/* XXX - Check to see if palm_errno == PALMERR_NOMEM.
		 * If so, then send an ACK with the PADP_FLAG_ERRNOMEM
		 * flag set.
		 */
		return err;		/* Error */
	/* XXX - At this point, we should check the packet to make
	 * sure it's a valid PADP packet, but I'm not sure what to do
	 * if it's not ("Never test for an error condition you don't
	 * know how to handle" :-) ).
	 */

	/* Parse the header */
	rptr = inbuf;
	header.type = get_ubyte(&rptr);
	header.flags = get_ubyte(&rptr);
	header.size = get_uword(&rptr);

	PADP_TRACE(5, "Got PADP message: type %d, flags 0x%02x, size %d\n",
		   header.type,
		   header.flags,
		   header.size);

#if PADP_DEBUG
	/* Dump the body, for debugging */
	if (padp_debug >= 6)
		debug_dump(stderr, "PADP <<<", inbuf+PADP_HEADER_LEN,
			   inlen-PADP_HEADER_LEN);
#endif	/* PADP_DEBUG */

	/* See what type of packet this is */
	switch (header.type)
	{
	    case PADP_FRAGTYPE_DATA:
		/* It's a data fragment. This is what we wanted */
		break;
	    case PADP_FRAGTYPE_ACK:
		/* XXX - I'm not sure what to do in this case. Drop
		 * the packet and wait for a new one?
		 */
fprintf(stderr, "##### I just got an unexpected ACK. I'm confused!\n");
		return -1;
	    case PADP_FRAGTYPE_TICKLE:
		/* Tickle packets aren't acknowledged, but the connection
		 * doesn't time out as long as they keep coming in. Just
		 * ignore it.
		 */
		goto retry;
	    case PADP_FRAGTYPE_ABORT:
		palm_errno = PALMERR_ABORT;
		return -1;
	    default:
		/* XXX */
fprintf(stderr, "##### Unexpected packet type %d\n", header.type);
		return -1;
	};

	/* XXX - If it's a single-fragment packet, just pass it up to
	 * the caller.
	 * The other possibility is that this is the first fragment of
	 * a multi-fragment packet. In that case, allocate a new
	 * buffer and read the other fragments in the packet. Assume
	 * that they come in order until it becomes necessary to
	 * assume otherwise.
	 * This check needs to be done here, because it's possible
	 * that there isn't enough memory to allocate the buffer. If
	 * so, the ACK for the first packet should have the
	 * PADP_FLAG_ERRNOMEM flag set.
	 */
/* XXX - For now, just handle the simple case */
if (((header.flags & PADP_FLAG_FIRST) == 0) ||
    ((header.flags & PADP_FLAG_LAST) == 0))
{
	fprintf(stderr, "+++++ I don't know how to handle multi-fragment packets (yet)\nDying at %s:%d", __FILE__, __LINE__);
	return -1;
}

	/* Send an ACK */
	/* Construct an output packet header and put it in 'outbuf' */
	wptr = outbuf;
	put_ubyte(&wptr, PADP_FRAGTYPE_ACK);
	put_ubyte(&wptr, header.flags);
	put_uword(&wptr, header.size);
	/* Set the transaction ID that the SLP layer will use when
	 * sending the ACK packet. This is a kludge, but is pretty
	 * much required, since SLP and PADP are rather tightly
	 * interwoven.
	 */
	pconn->padp.xid = pconn->slp.last_xid;

	PADP_TRACE(5, "Sending ACK: type %d, flags 0x%02x, size %d, xid 0x%02x\n",
		   PADP_FRAGTYPE_ACK,
		   header.flags,
		   header.size,
		   pconn->padp.xid);

	/* Send the ACK as a SLP packet */
	err = slp_write(fd, outbuf, PADP_HEADER_LEN);
	if (err < 0)
		return err;	/* An error has occurred */

	*buf = rptr;		/* Give the caller a pointer to the
				 * packet data */
	*len = header.size;	/* and say how much of it there was */

	return 1;		/* Success */
}

/* padp_write
 * Write the contents of 'buf', of length 'len' bytes, to the file
 * descriptor 'fd'.
 * If successful, returns a non-negative value. In case of error, returns a
 * negative value and sets 'palm_errno' to indicate the error.
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
	const ubyte *ack_buf;	/* Incoming buffer, for ACK packet */
	uword ack_len;		/* Length of ACK packet */
	struct padp_header ack_header;
				/* Parsed incoming ACK packet */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */
	int attempt;		/* Send attempt number */
	fd_set readfds;		/* File descriptors to read from, for
				 * select() */
	struct timeval timeout;	/* Timeout length, for select() */

	palm_errno = PALMERR_NOERR;

	/* Get the PConnection */
	if ((pconn = PConnLookup(fd)) == NULL)
	{
		fprintf(stderr, "padp_write: can't find PConnection for %d\n",
			fd);
		palm_errno = PALMERR_BADF;
		return -1;
	}

	/* XXX - Retries & timeouts */
	/* XXX - Fragmentation of long messages */
	/* XXX - Look at the length of the packet and see if it's
	 * longer than 1024 bytes. If so, send it as a multi-fragment
	 * message.
	 */
/* XXX - For now, just refuse to handle messages >= 1024 bytes */
if (len >= 1024)
{
	fprintf(stderr, "+++++ I don't know how to send packets > 1024 bytes.\nDying at %s:%d\n",
		__FILE__,__LINE__);
	return -1;
}

	bump_xid(pconn);		/* Pick a new transaction ID */

	/* Construct a header in 'outbuf' */
	wptr = outbuf;
	put_ubyte(&wptr, PADP_FRAGTYPE_DATA);	/* type */
	put_ubyte(&wptr, PADP_FLAG_FIRST |	/* flags */
		  PADP_FLAG_LAST);
		/* XXX - These flags are bogus for multi-fragment
		 * packets. */
	put_uword(&wptr, len);			/* size */
	PADP_TRACE(5, "Sending type %d, flags 0x%02x, size %d, xid 0x%02x\n",
		   PADP_FRAGTYPE_DATA,
		   PADP_FLAG_FIRST | PADP_FLAG_LAST,
		   len,
		   pconn->padp.xid);

	/* Append the caller's data to 'outbuf' */
	memcpy(outbuf+PADP_HEADER_LEN, buf, len);

	/* Set the timeout length, for select() */
	timeout.tv_sec = PADP_ACK_TIMEOUT;
	timeout.tv_usec = 0L;
	for (attempt = 0; attempt < PADP_MAX_RETRIES; attempt++)
	{
		/* Send 'outbuf' as a SLP packet */
		err = slp_write(fd, outbuf, PADP_HEADER_LEN+len);
		if (err < 0)
			return err;		/* Error */

		/* Get an ACK */
		/* Use select() to wait for the file descriptor to become
		 * readable. If nothing comes in, time out and retry.
		 */
		FD_ZERO(&readfds);
		FD_SET(pconn->fd, &readfds);
		err = select(pconn->fd+1, &readfds, NULL, NULL, &timeout);
		if (err == 0)
		{
			/* select() timed out */
			fprintf(stderr, "Timeout. Attempting to resend\n");
			continue;
		}
		err = slp_read(fd, &ack_buf, &ack_len);
		if (err == 0)
		{
			/* End of file */
			palm_errno = PALMERR_EOF;
			return -1;
		}
		if (err < 0)
			return err;		/* Error */

		/* Parse the ACK packet */
		rptr = ack_buf;
		ack_header.type = get_ubyte(&rptr);
		ack_header.flags = get_ubyte(&rptr);
		ack_header.size = get_uword(&rptr);
switch (ack_header.type)
{
    case PADP_FRAGTYPE_DATA:
	fprintf(stderr, "##### Got an unexpected data packet. I'm confused!\n");
	return -1;
    case PADP_FRAGTYPE_ACK:
	/* An ACK. Just what we wanted */
	break;
    case PADP_FRAGTYPE_TICKLE:
	/* Tickle packets aren't acknowledged, but the connection doesn't
	 * time out as long as they keep coming in. Just ignore it.
	 */
	attempt--;	/* XXX - Hack! */
	continue;
    case PADP_FRAGTYPE_ABORT:
	palm_errno = PALMERR_ABORT;
	return -1;
    default:
	/* XXX */
	fprintf(stderr, "##### Unexpected packet type %d\n", ack_header.type);
	return -1;
};
/* XXX - Presumably, if the XID on the received ACK doesn't match the
 * XID of the packet we sent, then we need to resend the original
 * packet.
 */
if (pconn->slp.last_xid != pconn->padp.xid)
{
fprintf(stderr, "##### Expected XID 0x%02x, got 0x%02x\n",
pconn->padp.xid, pconn->slp.last_xid);
return -1;
}
		PADP_TRACE(5, "Got an ACK: type %d, flags 0x%02x, size %d, xid 0x%02x\n",
			   ack_header.type,
			   ack_header.flags,
			   ack_header.size,
			   pconn->slp.last_xid);
		/* XXX - If it's not an ACK packet, assume that the ACK was
		 * sent and lost in transit, and that this is the response
		 * to the query we just sent.
		 */
		/* XXX - This whole section needs a lot of work. Need
		 * timeouts, retries, etc.
		 * One problem with the above code is the fixed-sized
		 * buffer. This will be a problem if we send a data packet,
		 * and the Palm sends an ACK, but the ACK gets lost on the
		 * way. In this case, the next PADP packet we receive will
		 * be a data packet. This implies that our ACK was received
		 * normally, so we should act as if we got the ACK in the
		 * first place. This means that we need to un-read the
		 * packet that just came in, return sucess, and get on with
		 * life.
		 */

		return 0;		/* Successfully got an ACK */
	}

	if (attempt >= PADP_MAX_RETRIES)
	{
		PADP_TRACE(5, "PADP: Reached retry limit. Abandoning.\n");
		palm_errno = PALMERR_TIMEOUT;
		return -1;
	}

	return 0;	/* XXX */
}
