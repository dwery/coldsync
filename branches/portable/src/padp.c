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
 * $Id: padp.c,v 1.2.2.1 1999-07-14 13:43:14 arensb Exp $
 */
#include "config.h"
#include <stdio.h>
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
#include <stdlib.h>			/* For free() */
#include "coldsync.h"
#include "palm_types.h"
#include "palm_errno.h"
#include "slp.h"
#include "padp.h"
#include "util.h"
#include "PConnection.h"

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

	/* Don't allocate a multi-fragment message buffer until it's
	 * necessary.
	 */
	pconn->padp.inbuf = NULL;
	pconn->padp.inbuf_len = 0L;

	return 0;
}

/* padp_tini
 * Clean up the PADP part of a PConnection that's being closed.
 */
int
padp_tini(struct PConnection *pconn)
{
	/* Free the buffer for multi-fragment messages, if there is one */
	if (pconn->padp.inbuf != NULL)
		free(pconn->padp.inbuf);
	return 0;
}

/* padp_read
 * Read a PADP packet from the given file descriptor. A pointer to the
 * packet data (without the PADP header) is put in '*buf'. The length
 * of the data (not counting the PADP header) is put in '*len'.
 *
 * If successful, returns a non-negative value. In case of error,
 * returns a negative value and sets 'palm_errno' to indicate the
 * error.
 */
int
padp_read(struct PConnection *pconn,	/* Connection to Palm */
	  const ubyte **buf,	/* Buffer to put the packet in */
	  uword *len)		/* Length of received message */
{
	int err;
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

	/* XXX - Multi-packet messages */

  retry:		/* It can take several attempts to read a packet,
			 * if the Palm sends tickles.
			 */
	/* Use select() to wait for the file descriptor to be readable. If
	 * nothing comes in in time, conclude that the connection is dead,
	 * and time out.
	 */
	timeout.tv_sec = pconn->padp.read_timeout;
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
	err = slp_read(pconn, &inbuf, &inlen);
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

	PADP_TRACE(5)
		fprintf(stderr,
			"Got PADP message: type %d, flags 0x%02x, size %d\n",
			header.type,
			header.flags,
			header.size);
	/* Dump the body, for debugging */
	PADP_TRACE(6)
		debug_dump(stderr, "PADP <<<", inbuf+PADP_HEADER_LEN,
			   inlen-PADP_HEADER_LEN);

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
		fprintf(stderr, "##### I just got an unexpected ACK. "
			"I'm confused!\n");
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
		fprintf(stderr, "##### Unexpected packet type %d\n",
			header.type);
		return -1;
	};

	/* XXX - If a fragment comes in with the 'last' flag set but not
	 * the 'first' flag, this'll get confused.
	 */
	if ((header.flags & (PADP_FLAG_FIRST | PADP_FLAG_LAST)) ==
	    (PADP_FLAG_FIRST | PADP_FLAG_LAST))
	{
		/* It's a single-fragment packet */

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

		PADP_TRACE(5)
			fprintf(stderr,
				"Sending ACK: type %d, flags 0x%02x, "
				"size %d, xid 0x%02x\n",
				PADP_FRAGTYPE_ACK,
				header.flags,
				header.size,
				pconn->padp.xid);

		/* Send the ACK as a SLP packet */
		err = slp_write(pconn, outbuf, PADP_HEADER_LEN);
		if (err < 0)
			return err;	/* An error has occurred */

		*buf = rptr;		/* Give the caller a pointer to the
					 * packet data */
		*len = header.size;	/* and say how much of it there was */

		return 0;		/* Success */
	} else {
		uword msg_len;		/* Total length of message */
		uword cur_offset;	/* Offset of the next expected
					 * fragment */

		/* XXX - Make sure the 'first' flag is set */

		/* It's a multi-fragment packet */
		PADP_TRACE(6)
			fprintf(stderr,
				"Got part 1 of a multi-fragment message\n");

		msg_len = header.size;	/* Total length of message */
		PADP_TRACE(7)
			fprintf(stderr, "MP: Total length == %d\n", msg_len);

		/* Allocate (or reallocate) a buffer in the PADP part of
		 * the PConnection large enough to hold the entire message.
		 */
		if (pconn->padp.inbuf == NULL)
		{
			PADP_TRACE(7)
				fprintf(stderr,
					"MP: Allocating new MP buffer\n");
			/* Allocate a new buffer */
			if ((pconn->padp.inbuf = (ubyte *) malloc(msg_len))
			    == NULL)
			{
				PADP_TRACE(7)
					fprintf(stderr,
						"MP: Can't allocate new MP buffer\n");
				palm_errno = PALMERR_NOMEM;
				/* XXX - Should return an ACK to the Palm
				 * that says we're out of memory.
				 */
				return -1;
			}
		} else {
			/* Resize the existing buffer to a new size */
			ubyte *eptr;	/* Pointer to reallocated buffer */

			PADP_TRACE(7)
				fprintf(stderr,
					"MP: Resizing existing MP buffer\n");
			if ((eptr = (ubyte *) realloc(pconn->padp.inbuf,
						      msg_len)) == NULL)
			{
				PADP_TRACE(7)
					fprintf(stderr,
						"MP: Can't resize existing MP buffer\n");
				palm_errno = PALMERR_NOMEM;
				return -1;
			}
			pconn->padp.inbuf = eptr;
			pconn->padp.inbuf_len = msg_len;
		}

		/* Copy the first fragment to the PConnection buffer */
		memcpy(pconn->padp.inbuf, rptr, inlen-PADP_HEADER_LEN);
		cur_offset = inlen-PADP_HEADER_LEN;
		PADP_TRACE(7)
			fprintf(stderr,
				"MP: Copied first fragment. cur_offset == %d\n",
				cur_offset);

		/* Send an ACK for the first fragment */
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

		PADP_TRACE(5)
			fprintf(stderr,
				"Sending ACK: type %d, flags 0x%02x, "
				"size %d, xid 0x%02x\n",
				PADP_FRAGTYPE_ACK,
				header.flags,
				header.size,
				pconn->padp.xid);

		/* Send the ACK as a SLP packet */
		err = slp_write(pconn, outbuf, PADP_HEADER_LEN);
		if (err < 0)
			return err;	/* An error has occurred */

		/* Get the rest of the message */
		do {
			PADP_TRACE(7)
				fprintf(stderr,
					"MP: Waiting for more fragments\n");
			/* Receive a new fragment */
		  mpretry:		/* It can take several attempts to
					 * read a packet, if the Palm sends
					 * tickles.
					 */

			/* Use select() to wait for the file descriptor to
			 * be readable. If nothing comes in in time,
			 * conclude that the connection is dead, * and time
			 * out.
			 */
			timeout.tv_sec = pconn->padp.read_timeout / 10;
			timeout.tv_usec = 0L;		/* Set the timeout */

			FD_ZERO(&readfds);		/* Set of file
							 * descriptors to
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
			err = slp_read(pconn, &inbuf, &inlen);
			if (err == 0)
				return 0;	/* End of file: no data
						 * read */
			if (err < 0)
				/* XXX - Check to see if palm_errno ==
				 * PALMERR_NOMEM. If so, then send an ACK
				 * with the PADP_FLAG_ERRNOMEM flag set.
				 */
				return err;		/* Error */

			/* XXX - At this point, we should check the packet
			 * to make sure it's a valid PADP packet, but I'm
			 * not sure what to do if it's not ("Never test
			 * for an error condition you don't know how to
			 * handle" :-) ).
			 */

			/* Parse the header */
			rptr = inbuf;
			header.type = get_ubyte(&rptr);
			header.flags = get_ubyte(&rptr);
			header.size = get_uword(&rptr);

			PADP_TRACE(5)
				fprintf(stderr,
					"Got PADP message: type %d, "
					"flags 0x%02x, size %d\n",
					header.type,
					header.flags,
					header.size);

			/* Dump the body, for debugging */
			PADP_TRACE(6)
				debug_dump(stderr,
					   "PADP <<<",
					   inbuf+PADP_HEADER_LEN,
					   inlen-PADP_HEADER_LEN);

			/* See what type of packet this is */
			switch (header.type)
			{
			    case PADP_FRAGTYPE_DATA:
				/* It's a data fragment. This is what we
				 * wanted.
				 */
				break;
			    case PADP_FRAGTYPE_ACK:
				/* XXX - I'm not sure what to do in this
				 * case. Drop the packet and wait for a new
				 * one?
				 */
				fprintf(stderr, "##### I just got an unexpected ACK. I'm confused!\n");
				return -1;
			    case PADP_FRAGTYPE_TICKLE:
				/* Tickle packets aren't acknowledged, but
				 * the connection doesn't time out as long
				 * as they keep coming in. Just ignore it.
				 */
				goto mpretry;
			    case PADP_FRAGTYPE_ABORT:
				palm_errno = PALMERR_ABORT;
				return -1;
			    default:
				/* XXX */
				fprintf(stderr, "##### Unexpected packet type %d\n", header.type);
				return -1;
			};

			/* If it's new, then I'm confused */
			if (header.flags & PADP_FLAG_FIRST)
			{
				fprintf(stderr, "##### I wasn't expecting a new fragment. I'm confused!\n");
				/* palm_errno = XXX */
				return -1;
			}
			PADP_TRACE(7)
				fprintf(stderr,
					"MP: It's not a new fragment\n");

			if (header.size != cur_offset)
			{
				/* XXX */
				fprintf(stderr, "##### Bad offset: wanted %d, got %d\n",
					cur_offset, header.size);
				return -1;
			}
			PADP_TRACE(7)
				fprintf(stderr,
					"MP: It goes at the right offset\n");

			/* Copy fragment to pconn->padp.inbuf */
			memcpy(pconn->padp.inbuf+cur_offset, rptr,
			       inlen-PADP_HEADER_LEN);
			PADP_TRACE(7)
				fprintf(stderr,
					"MP: Copies this fragment to inbuf+%d\n",
					cur_offset);

			/* Update cur_offset */
			cur_offset += inlen-PADP_HEADER_LEN;

			/* Acknowledge the fragment */
			/* Construct an output packet header and put it in
			 * 'outbuf'
			 */
			wptr = outbuf;
			put_ubyte(&wptr, PADP_FRAGTYPE_ACK);
			put_ubyte(&wptr, header.flags);
			put_uword(&wptr, header.size);
			/* Set the transaction ID that the SLP layer will
			 * use when sending the ACK packet. This is a
			 * kludge, but is pretty much required, since SLP
			 * and PADP are rather tightly interwoven.
			 */
			pconn->padp.xid = pconn->slp.last_xid;

			PADP_TRACE(5)
				fprintf(stderr,
					"Sending ACK: type %d, "
					"flags 0x%02x, size %d, xid 0x%02x\n",
					PADP_FRAGTYPE_ACK,
					header.flags,
					header.size,
					pconn->padp.xid);

			/* Send the ACK as a SLP packet */
			err = slp_write(pconn, outbuf, PADP_HEADER_LEN);
			if (err < 0)
				return err;	/* An error has occurred */

		} while ((header.flags & PADP_FLAG_LAST) == 0);
		PADP_TRACE(7)
			fprintf(stderr,
				"MP: That was the last fragment. "
				"Returning:\n");

		/* Return the message to the caller */
		*buf = pconn->padp.inbuf;	/* Message data */
		*len = msg_len;			/* Message length */
		PADP_TRACE(10)
		{
			fprintf(stderr, "\tlen == %d\n", *len);
			debug_dump(stderr, "+MP", *buf, *len);
		}
		return 0;
	}

	/* XXX - Is this point ever reached? */
}

/* padp_write
 * Write a (possibly multi-fragment) message.
 */
int
padp_write(struct PConnection *pconn,
		 ubyte *buf,
		 uword len)
{
	int err;
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
	fd_set writefds;	/* File descriptors to write to, for
				 * select() */
	struct timeval timeout;	/* Timeout length, for select() */
	uword offset;		/* Current offset */

	palm_errno = PALMERR_NOERR;

	bump_xid(pconn);	/* Pick a new transmission ID */

	for (offset = 0; offset < len; offset += PADP_MAX_PACKET_LEN)
	{
		ubyte frag_flags;	/* Flags for this fragment */
		uword frag_len;		/* Length of this fragment */

		PADP_TRACE(6)
			fprintf(stderr, "offset == %d (of %d)\n", offset, len);
		frag_flags = 0;

		if (offset == 0)
		{
			/* It's the first fragment */
			frag_flags |= PADP_FLAG_FIRST;
		}

		if ((len - offset) <= PADP_MAX_PACKET_LEN)
		{
			/* It's the last fragment */
			frag_flags |= PADP_FLAG_LAST;
			frag_len = len - offset;
		} else {
			/* It's not the last fragment */
			frag_len = PADP_MAX_PACKET_LEN;
		}
		PADP_TRACE(7)
			fprintf(stderr,
				"frag_flags == 0x%02x, frag_len == %d\n",
				frag_flags, frag_len);

		/* Construct a header in 'outbuf' */
		wptr = outbuf;
		put_ubyte(&wptr, PADP_FRAGTYPE_DATA);	/* type */
		put_ubyte(&wptr, frag_flags);		/* flags */
		if (frag_flags & PADP_FLAG_FIRST)
			put_uword(&wptr, len);
		else
			put_uword(&wptr, offset);
		memcpy(outbuf+PADP_HEADER_LEN, buf+offset, frag_len);

		PADP_TRACE(5)
			fprintf(stderr, "Sending type %d, flags 0x%02x, "
				"size %d, xid 0x%02x\n",
				PADP_FRAGTYPE_DATA,
				frag_flags,
				frag_len,
				pconn->padp.xid);

		/* Set the timeout length, for select() */
		timeout.tv_sec = PADP_ACK_TIMEOUT;
		timeout.tv_usec = 0L;

		for (attempt = 0; attempt < PADP_MAX_RETRIES; attempt++)
		{
			FD_ZERO(&writefds);
			FD_SET(pconn->fd, &writefds);
			err = select(pconn->fd+1, NULL, &writefds, NULL,
				     &timeout);
			if (err == 0)
			{
				/* select() timed out */
				fprintf(stderr, "Write timeout. Attempting to resend.\n");
				continue;
			}
			PADP_TRACE(6)
				fprintf(stderr, "about to slp_write()\n");

			/* Send 'outbuf' as a SLP packet */
			err = slp_write(pconn, outbuf,
					PADP_HEADER_LEN+frag_len);
			if (err < 0)
				return err;		/* Error */

			/* Get an ACK */
			/* Use select() to wait for the file descriptor to
			 * become readable. If nothing comes in, time out
			 * and retry.
			 */
			FD_ZERO(&readfds);
			FD_SET(pconn->fd, &readfds);
			err = select(pconn->fd+1,
				     &readfds, NULL, NULL,
				     &timeout);
			if (err == 0)
			{
				/* select() timed out */
				fprintf(stderr, "Timeout. Attempting to resend\n");
				continue;
			}
			err = slp_read(pconn, &ack_buf, &ack_len);
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
				/* Tickle packets aren't acknowledged, but
				 * the connection doesn't time out as long
				 * as they keep coming in. Just ignore it.
				 */
				attempt--;	/* XXX - Hack! */
				continue;
			    case PADP_FRAGTYPE_ABORT:
				palm_errno = PALMERR_ABORT;
				return -1;
			    default:
				/* XXX */
				fprintf(stderr, "##### Unexpected packet type %d\n",
					ack_header.type);
				return -1;
			};

			/* XXX - Presumably, if the XID on the received ACK
			 * doesn't match the XID of the packet we sent,
			 * then we need to resend the original packet.
			 */
			if (pconn->slp.last_xid != pconn->padp.xid)
			{
				fprintf(stderr, "##### Expected XID 0x%02x, "
					"got 0x%02x\n",
					pconn->padp.xid, pconn->slp.last_xid);
				return -1;
			}
			PADP_TRACE(5)
				fprintf(stderr,
					"Got an ACK: type %d, flags 0x%02x, "
					"size %d, xid 0x%02x\n",
					ack_header.type,
					ack_header.flags,
					ack_header.size,
					pconn->slp.last_xid);
			/* XXX - If it's not an ACK packet, assume that the
			 * ACK was sent and lost in transit, and that this
			 * is the response to the query we just sent.
			 */

			break;			/* Successfully got an ACK */
		}

		if (attempt >= PADP_MAX_RETRIES)
		{
			PADP_TRACE(5)
				fprintf(stderr,
					"PADP: Reached retry limit. "
					"Abandoning.\n");
			palm_errno = PALMERR_TIMEOUT;
			return -1;
		}
		PADP_TRACE(7)
			fprintf(stderr, "Bottom of offset-loop\n");
	}
	PADP_TRACE(7)
		fprintf(stderr, "After offset-loop\n");

	return 0;
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */