/* padp.c
 *
 * Implementation of the Palm PADP (Packet Assembly/Disassembly
 * Protocol).
 *
 *	Copyright (C) 1999, 2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * Note on nomenclature: the term 'packet' is used somewhat loosely
 * throughout this file, and can mean "data passed from a protocol
 * further up the stack" or "data sent down to a protocol further down
 * the stack (SLP)", or something else, depending on context.
 *
 * $Id: padp.c,v 1.16 2000-12-24 21:24:39 arensb Exp $
 */
#include "config.h"
#include <stdio.h>

#include <stdlib.h>			/* For free() */

#if STDC_HEADERS
# include <string.h>		/* For memcpy() et al. */
#else	/* STDC_HEADERS */
# ifndef HAVE_STRCHR
#  define strchr index
#  define strrchr rindex
# endif	/* HAVE_STRCHR */
# ifndef HAVE_MEMCPY
#  define memcpy(d,s,n)		bcopy ((s), (d), (n))
#  define memmove(d,s,n)	bcopy ((s), (d), (n))
# endif	/* HAVE_MEMCPY */
#endif	/* STDC_HEADERS */

#if HAVE_LIBINTL_H
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL_H */

#include "pconn/palm_types.h"
#include "pconn/palm_errno.h"
#include "pconn/slp.h"
#include "pconn/padp.h"
#include "pconn/util.h"
#include "pconn/PConnection.h"

int padp_trace = 0;		/* Debugging level for PADP */
#define PADP_TRACE(n)	if (padp_trace >= (n))

/* bump_xid
 * Pick a new transaction ID by incrementing the existing one, and
 * skipping over the reserved ones (AFAIK, 0xff and 0x00 are
 * reserved).
 */
static void
bump_xid(PConnection *pconn)
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
padp_init(PConnection *pconn)
{
	pconn->padp.read_timeout = PADP_WAIT_TIMEOUT;
				/* How long to wait for a PADP packet
				 * to arrive */

	/* Don't allocate a multi-fragment message buffer until it's
	 * necessary.
	 */
	pconn->padp.inbuf = NULL;
	pconn->padp.inbuf_len = 0L;

	/* Set the functions to send and receive DLP packets */
	pconn->dlp.read = padp_read;
	pconn->dlp.write = padp_write;

	return 0;
}

/* padp_tini
 * Clean up the PADP part of a PConnection that's being closed.
 */
int
padp_tini(PConnection *pconn)
{
	if (pconn == NULL)
		return 0;

	/* Free the buffer for multi-fragment messages, if there is one */
	if (pconn->padp.inbuf != NULL)
		free(pconn->padp.inbuf);
	return 0;
}

/* padp_read
 * Read a PADP packet from the given PConnection. A pointer to the packet
 * data (without the PADP header) is put in '*buf'. The length of the data
 * (not counting the PADP header) is put in '*len'.
 *
 * If successful, returns a non-negative value. In case of error, returns a
 * negative value and sets 'palm_errno' to indicate the error.
 */
int
padp_read(PConnection *pconn,	/* Connection to Palm */
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
	struct timeval timeout;	/* Read timeout, for select() */

	palm_errno = PALMERR_NOERR;

  retry:		/* It can take several attempts to read a packet,
			 * if the Palm sends tickles.
			 */
	/* Use select() to wait for the file descriptor to be readable. If
	 * nothing comes in in time, conclude that the connection is dead,
	 * and time out.
	 */
	timeout.tv_sec = pconn->padp.read_timeout;
	timeout.tv_usec = 0L;		/* Set the timeout */

	err = (*pconn->io_select)(pconn, forReading, &timeout);
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
		/* Received a bogus ACK packet. ACK packets aren't
		 * themselves acknowledged, so don't send anything back to
		 * the Palm.
		 * Retrying the read may not be the Right Thing to do, but
		 * it might add robustness.
		 */
		fprintf(stderr, _("##### I just got an unexpected ACK. "
			"I'm confused!\n"));
		goto retry;
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
		fprintf(stderr, _("##### Unexpected packet type %d\n"),
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
		/* XXX - dump the ACK */
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
			 * conclude that the connection is dead, and time
			 * out.
			 */
			timeout.tv_sec = pconn->padp.read_timeout / 10;
			timeout.tv_usec = 0L;		/* Set the timeout */

			err = (*pconn->io_select)(pconn, forReading, &timeout);
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
				/* It's an ACK packet, which doesn't
				 * belong here.
				 * As in the case of the first fragment, I
				 * really don't know that retrying is the
				 * Right Thing to do, but it's all I can
				 * think of right now, and who knows? It
				 * might help.
				 */
				fprintf(stderr,
					_("##### I just got an unexpected "
					  "ACK. I'm confused!\n"));
				goto mpretry;
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
				fprintf(stderr,
					_("##### Unexpected packet type "
					  "%d\n"),
					header.type);
				return -1;
			};

			/* If it's new, then I'm confused */
			if (header.flags & PADP_FLAG_FIRST)
			{
				fprintf(stderr,
					_("##### I wasn't expecting a new "
					  "fragment. I'm confused!\n"));
				/* palm_errno = XXX */
				return -1;
			}
			PADP_TRACE(7)
				fprintf(stderr,
					"MP: It's not a new fragment\n");

			if (header.size != cur_offset)
			{
				/* XXX */
				fprintf(stderr,
					_("##### Bad offset: wanted %d, got "
					  "%d\n"),
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
					"MP: Copied this fragment to "
					"inbuf+%d\n",
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
padp_write(PConnection *pconn,
	   const ubyte *buf,
	   const uword len)
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

		/* Try to send the packet */
		for (attempt = 0; attempt < PADP_MAX_RETRIES; attempt++)
		{
			ubyte ackout[PADP_HEADER_LEN];
					/* Buffer in which to construct an
					 * ACK packet, in case we need to
					 * send one (see unexpected data
					 * packet, below).
					 */
			ubyte *ackoutptr;	/* Pointer into 'ackout' */

		  mpretry:
			/* Set the timeout length, for select() */
			timeout.tv_sec = PADP_ACK_TIMEOUT;
			timeout.tv_usec = 0L;

			err = (*pconn->io_select)(pconn, forWriting, &timeout);
			if (err == 0)
			{
				/* select() timed out */
				fprintf(stderr,
					_("Write timeout. Attempting to "
					  "resend.\n"));
				continue;
			}
			PADP_TRACE(6)
				fprintf(stderr, "about to slp_write()\n");
			PADP_TRACE(6)
				debug_dump(stderr, "PADP >>>", outbuf,
					   PADP_HEADER_LEN+frag_len);

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
			timeout.tv_sec = PADP_ACK_TIMEOUT;
			timeout.tv_usec = 0L;

			err = (*pconn->io_select)(pconn, forReading, &timeout);
			if (err == 0)
			{
				/* select() timed out */
				fprintf(stderr,
					_("ACK Timeout. Attempting to "
					  "resend\n"));
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

			PADP_TRACE(7)
				debug_dump(stderr, "ACK <<<", ack_buf,
					   /* An ACK packet is basically
					    * just a PADP header, so ignore
					    * the size field.
					    */
					   PADP_HEADER_LEN);

			switch (ack_header.type)
			{
			    case PADP_FRAGTYPE_DATA:
				/* This might happen if:
				 * Palm sends data packet to PC.
				 * PC sends ACK, but it gets lost.
				 * PC sends data packet.
				 * PC expects ACK.
				 * Palm never saw the original ACK, so it
				 *    resends the previous data packet.
				 *
				 * This might also happen if someone killed
				 * a coldsync process and started a new one
				 * without telling the Palm: the first
				 * thing the second coldsync would see
				 * would be a PADP data packet.
				 *
				 * The problem is that we have no idea what
				 * the Palm is trying to resend, so
				 * presumably the least bad thing to do is
				 * to send the Palm an ACK so it'll shut up
				 * and let us go on with what we want to
				 * send.
				 */
				fprintf(stderr,
					_("##### Got an unexpected data "
					  "packet. Sending an ACK to shut "
					  "it up.\n"));
				PADP_TRACE(5)
					fprintf(stderr,
						"sending ACK: type %d, flags "
						"0x%02x, size 0x%02x, xid "
						"0x%02x\n",
						PADP_FRAGTYPE_ACK,
						ack_header.flags,
						ack_header.size,
						pconn->slp.last_xid);
				ackoutptr = ackout;
				put_ubyte(&ackoutptr, PADP_FRAGTYPE_ACK);
				put_ubyte(&ackoutptr, ack_header.flags);
				put_uword(&ackoutptr, ack_header.size);
				pconn->padp.xid = pconn->slp.last_xid;

				/* Send the ACK */
				err = slp_write(pconn, ackout,
						PADP_HEADER_LEN);
				if (err < 0)
				{
					/* If even this gives us an error,
					 * things are seriously fscked up.
					 */
					fprintf(stderr,
						_("%s: Error sending dummy "
						  "ACK. This is serious.\n"),
						"padp_write");
					return -1;
				}
				bump_xid(pconn);
					/* Increment SLP XID, so we don't
					 * reuse the one from this
					 * ACK.
					 */

				/* Try sending this fragment again */
				goto mpretry;
			    case PADP_FRAGTYPE_ACK:
				/* An ACK. Just what we wanted */
				break;
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
				fprintf(stderr,
					_("##### Unexpected packet type %d\n"),
					ack_header.type);
				return -1;
			};

			/* XXX - Presumably, if the XID on the received ACK
			 * doesn't match the XID of the packet we sent,
			 * then we need to resend the original packet.
			 */
			if (pconn->slp.last_xid != pconn->padp.xid)
			{
				fprintf(stderr, _("##### Expected XID 0x%02x, "
					"got 0x%02x\n"),
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
