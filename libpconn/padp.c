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
 * $Id: padp.c,v 1.25 2004-03-27 15:25:03 azummo Exp $
 */

/*
 *  Debug info:
 *
 *  1:
 *  2:
 *  3: error conditions
 *  4: functions entry point
 *  5: outer loop status 
 *  6: padp header dump
 *  7: inner loop status
 *  8: MP info
 *  9: loop exit info
 * 10: padp payload dump
 *
 */

/* XXX some functions in this file are still undocumented */

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

#include "palm.h"
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
		pconn->padp.xid = 0x01;
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

char *
padptype(padp_frag_t type)
{
	switch ((padp_frag_t) type)
	{
	    case PADP_FRAGTYPE_DATA:
		return "DATA";

	    case PADP_FRAGTYPE_ACK:
		return "ACK";

	    case PADP_FRAGTYPE_TICKLE:
		return "TICKLE";
		
	    case PADP_FRAGTYPE_ABORT:
		return "ABORT";

	    case PADP_FRAGTYPE_NAK:
		return "NAK";

	    default:
		return "UNKNOWN";
	}
}

int
padp_tx_frag(PConnection *pconn,
		struct padp_header *header,
		const uword buf_len,
		const ubyte *buf)
{
	int err;
	static ubyte outbuf[PADP_HEADER_LEN+PADP_MAX_PACKET_LEN];
		                /* Outgoing buffer */
        ubyte *wptr;            /* Pointer into buffers (for writing) */

	/* Construct a header in 'outbuf' */
	wptr = outbuf;
	put_ubyte(&wptr, (ubyte) header->type);	/* type */
	put_ubyte(&wptr, header->flags);	/* flags */
	put_uword(&wptr, header->size);	/* size */

	/* Copy the optional payload into the output buffer */

	if (buf && buf_len > 0)
	{
		memcpy(outbuf+PADP_HEADER_LEN, buf, buf_len);

		PADP_TRACE(10)
			debug_dump(stderr, "PADP >>>", outbuf,
				   PADP_HEADER_LEN+buf_len);
	}

	PADP_TRACE(6)
		fprintf(stderr,
			"PADP TX: flags %c%c (0x%02x), type %s, "
			"size %d, buf_len %d, xid 0x%02x\n",

			header->flags & PADP_FLAG_FIRST ? 'F' : ' ',
			header->flags & PADP_FLAG_LAST  ? 'L' : ' ',
			header->flags,

			padptype(header->type),
			
			header->size,
			buf_len,
			pconn->padp.xid);



	/* Send 'outbuf' as a SLP packet */
	err = slp_write(pconn, outbuf,
			PADP_HEADER_LEN+buf_len);
	if (err < 0)
		return err;		/* Error */

	return 0;
}

int
padp_ack(PConnection *pconn, ubyte flags, uword size)
{
	struct padp_header header;

	/* Header setup */

	header.type	= PADP_FRAGTYPE_ACK;
	header.flags	= 0;
	header.size	= size;
	
	/* Set the transaction ID that the SLP layer will use when
	 * sending the ACK packet. This is a kludge, but is pretty
	 * much required, since SLP and PADP are rather tightly
	 * interwoven.
	 */
	pconn->padp.xid = pconn->slp.last_xid;

	return padp_tx_frag(pconn,
		&header,
		0,
		NULL);
}

int
padp_rx_frag(PConnection *pconn,	/* Connection to Palm */
	struct padp_header *header,	/* Header of incoming packet */	
	const ubyte **buf,		/* Buffer to put the packet in */
	uword *len)			/* Length of received message */
{
	int err;
	const ubyte *inbuf;		/* Incoming data, from SLP layer */
	uword inlen;			/* Length of incoming data */
	const ubyte *rptr;		/* Pointer into buffers (for reading) */

	pconn->palm_errno = PALMERR_NOERR;

	/* Use select() to wait for the file descriptor to be readable. If
	 * nothing comes in in time, conclude that the connection is dead,
	 * and time out.
	 */
	err = PConn_timedselect(pconn, forReading, pconn->padp.read_timeout);

	if (err == 0)
	{
		/* select() timed out */
		PADP_TRACE(3)
			fprintf(stderr, "padp_rx_frag: timeout while reading\n");
		return -1;
	}

	/* Read an SLP packet */
	err = slp_read(pconn, &inbuf, &inlen);
	if (err == 0)
	{
		PADP_TRACE(3)
			fprintf(stderr, "padp_read: EOF\n");
		return -1;		/* End of file: no data read */
	}
	if (err < 0)
		/* XXX - Check to see if pconn->palm_errno == PALMERR_NOMEM.
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
	header->type	= get_ubyte(&rptr);
	header->flags	= get_ubyte(&rptr);
	header->size	= get_uword(&rptr);

	PADP_TRACE(6)
		fprintf(stderr,
			"PADP RX: flags %c%c (0x%02x), type %s, "
			"size %d, xid 0x%02x\n",

			header->flags & PADP_FLAG_FIRST ? 'F' : ' ',
			header->flags & PADP_FLAG_LAST  ? 'L' : ' ',
			header->flags,

			padptype(header->type),
			
			header->size,
			pconn->padp.xid);


	/* Dump the body, for debugging */
	PADP_TRACE(10)
		debug_dump(stderr, "PADP <<<", inbuf+PADP_HEADER_LEN,
			   inlen-PADP_HEADER_LEN);

	*len = inlen-PADP_HEADER_LEN;
	*buf = inbuf+PADP_HEADER_LEN;

	return 1;
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
	struct padp_header header;
				/* Header of incoming packet */
        const ubyte *inbuf;     /* Incoming data, from SLP layer */
       	uword inlen;		/* Length of incoming data */

	pconn->palm_errno = PALMERR_NOERR;

	PADP_TRACE(4)
		fprintf(stderr, "padp_read\n");

  retry:		/* It can take several attempts to read a packet,
			 * if the Palm sends tickles.
			 */

				 

	err = padp_rx_frag(pconn,
		&header,
		&inbuf,
		&inlen);
	
	if (err < 0)
		return err;
			 
	/* See what type of packet this is */
	switch ((padp_frag_t) header.type)
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
		pconn->palm_errno = PALMERR_ABORT;
		return -1;

	    case PADP_FRAGTYPE_NAK:
		/* No longer used. Silently ignore this */
		goto retry;

	    default:
		/* XXX */
		fprintf(stderr, _("##### Unexpected packet type %d.\n"),
			header.type);
		return -1;
	};

	/* XXX - If a fragment comes in with the 'last' flag set but not
	 * the 'first' flag, this'll get confused.
	 */

	if ((header.flags & PADP_FLAG_LAST) && !((header.flags & PADP_FLAG_FIRST)))
		fprintf(stderr, "papd_read: flags error, please contact the developers.\n");

	if ((header.flags & (PADP_FLAG_FIRST | PADP_FLAG_LAST)) ==
	    (PADP_FLAG_FIRST | PADP_FLAG_LAST))
	{
		/* It's a single-fragment packet */

		/* Send an ACK */
		padp_ack(pconn, header.flags, header.size);

		*buf = inbuf;		/* Give the caller a pointer to the
					 * packet data */
		*len = header.size;	/* and say how much of it there was */

		return 0;		/* Success */
	} else {
		uword msg_len;		/* Total length of message */
		uword cur_offset;	/* Offset of the next expected
					 * fragment */

		/* XXX - Make sure the 'first' flag is set */

		/* It's a multi-fragment packet */
		PADP_TRACE(5)
			fprintf(stderr,
				"MP: Got part 1 of a multi-fragment message\n");

		msg_len = header.size;	/* Total length of message */
		PADP_TRACE(5)
			fprintf(stderr, "MP: Total length == %d\n", msg_len);

		/* Allocate (or reallocate) a buffer in the PADP part of
		 * the PConnection large enough to hold the entire message.
		 */
		if (pconn->padp.inbuf == NULL)
		{
			PADP_TRACE(8)
				fprintf(stderr,
					"MP: Allocating new MP buffer\n");
			/* Allocate a new buffer */
			if ((pconn->padp.inbuf = (ubyte *) malloc(msg_len))
			    == NULL)
			{
				PADP_TRACE(3)
					fprintf(stderr,
						"MP: Can't allocate new "
						"MP buffer\n");
				pconn->palm_errno = PALMERR_NOMEM;
				/* XXX - Should return an ACK to the Palm
				 * that says we're out of memory.
				 */
				return -1;
			}
		} else {
			/* Resize the existing buffer to a new size */
			ubyte *eptr;	/* Pointer to reallocated buffer */

			PADP_TRACE(8)
				fprintf(stderr,
					"MP: Resizing existing MP buffer\n");
			if ((eptr = (ubyte *) realloc(pconn->padp.inbuf,
						      msg_len)) == NULL)
			{
				PADP_TRACE(3)
					fprintf(stderr,
						"MP: Can't resize existing "
						"MP buffer\n");
				pconn->palm_errno = PALMERR_NOMEM;
				return -1;
			}
			pconn->padp.inbuf = eptr;
			pconn->padp.inbuf_len = msg_len;
		}

		/* Copy the first fragment to the PConnection buffer */
		memcpy(pconn->padp.inbuf, inbuf, inlen);
		cur_offset = inlen;
		PADP_TRACE(8)
			fprintf(stderr,
				"MP: Copied first fragment. cur_offset == "
				"%d\n",
				cur_offset);

		/* Send an ACK for the first fragment */
		padp_ack(pconn, header.flags, header.size);

		/* Get the rest of the message */
		do {
			PADP_TRACE(8)
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
			err = PConn_timedselect(pconn, forReading, pconn->padp.read_timeout);
		
			if (err == 0)
				return -1;


			err = padp_rx_frag(pconn,
				&header,
				&inbuf,
				&inlen);
	
			if (err < 0)
				return err;


			/* See what type of packet this is */
			switch ((padp_frag_t) header.type)
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
				pconn->palm_errno = PALMERR_ABORT;
				return -1;
			    default:
				/* XXX */
				fprintf(stderr,
					_("##### Unexpected packet type "
					  "%d.\n"),
					header.type);
				return -1;
			};

			/* If it's new, then I'm confused */
			if (header.flags & PADP_FLAG_FIRST)
			{
				fprintf(stderr,
					_("##### I wasn't expecting a new "
					  "fragment. I'm confused!\n"));
				/* pconn->palm_errno = XXX */
				return -1;
			}
			PADP_TRACE(8)
				fprintf(stderr,
					"MP: It's not a new fragment\n");

			if (header.size != cur_offset)
			{
				/* XXX */
				fprintf(stderr,
					_("##### Bad offset: wanted %d, got "
					  "%d.\n"),
					cur_offset, header.size);
				return -1;
			}
			PADP_TRACE(8)
				fprintf(stderr,
					"MP: It goes at the right offset\n");

			/* Copy fragment to pconn->padp.inbuf */
			memcpy(pconn->padp.inbuf+cur_offset, inbuf,
			       inlen);
			PADP_TRACE(8)
				fprintf(stderr,
					"MP: Copied this fragment to "
					"inbuf+%d\n",
					cur_offset);

			/* Update cur_offset */
			cur_offset += inlen;

			/* Acknowledge the fragment */
			padp_ack(pconn, header.flags, header.size);
			
		} while ((header.flags & PADP_FLAG_LAST) == 0);
		PADP_TRACE(8)
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

	fprintf(stderr,
		"Point Of No Return reached, please contact the developers.\n");

	/* XXX - Is this point ever reached? */
}


/* padp_write
 * Write a (possibly multi-fragment) message.
 */
int
padp_write(PConnection *pconn,
	   const ubyte *buf,
	   const uword buf_len)
{
	int err;
	const ubyte *ack_buf;	/* Incoming buffer, for ACK packet */
	uword ack_len;		/* Length of ACK packet */
	struct padp_header ack_header;
				/* Parsed incoming ACK packet */
	int attempt;		/* Send attempt number */
	udword offset;		/* Current offset (it's an udword to avoid roll-overs) */

	pconn->palm_errno = PALMERR_NOERR;

	bump_xid(pconn);	/* Pick a new transmission ID */

	PADP_TRACE(4)
		fprintf(stderr, "padp_write: len = %d\n", buf_len);

	for (offset = 0; offset < buf_len; offset += PADP_MAX_PACKET_LEN)
	{
		struct padp_header header;

		uword frag_len;		/* Length of this fragment */

		PADP_TRACE(5)
			fprintf(stderr, "padp_write: tx offset == %ld (of %d)\n", offset, buf_len);

		/* Header setup */

		header.type	= PADP_FRAGTYPE_DATA;
		header.flags	= 0;

		if (offset == 0)
		{
			/* It's the first fragment */
			header.flags |= PADP_FLAG_FIRST;
		}

		if ((buf_len - offset) <= PADP_MAX_PACKET_LEN)
		{
			/* It's the last fragment */
			header.flags |= PADP_FLAG_LAST;
			frag_len = buf_len - offset;
		} else {
			/* It's not the last fragment */
			frag_len = PADP_MAX_PACKET_LEN;
		}

		header.size = (header.flags & PADP_FLAG_FIRST) ? buf_len : offset;

		for (attempt = 0; attempt < PADP_MAX_RETRIES; attempt++)
		{
			PADP_TRACE(7)
				fprintf(stderr, "padp_write: attempt %d\n", attempt);
	mpretry:

			err = PConn_timedselect(pconn, forWriting, PADP_ACK_TIMEOUT);
	
			if (err == 0)
			{
			 	/* select() timed out */
	 			fprintf(stderr,
			 	        _("Write timeout. Attempting to "
	 			          "resend.\n"));
			 	continue;
			}

			/* Send */

			err = padp_tx_frag(pconn,
				&header,
				frag_len,
				buf+offset);

			if (err < 0)
				return err;
			

			/* Wait an ACK */
		
			/* Use select() to wait for the file descriptor to
			 * become readable. If nothing comes in, time out
			 * and retry.
			 */
			err = PConn_timedselect(pconn, forReading, PADP_ACK_TIMEOUT);

			if (err == 0)
			{
				/* select() timed out */
				fprintf(stderr,
					_("ACK Timeout. Attempting to "
					  "resend.\n"));
				continue;
			}

			err = padp_rx_frag(pconn,
				&ack_header,
				&ack_buf,
				&ack_len);
	
			if (err < 0)
				return err;
			 

			switch ((padp_frag_t) ack_header.type)
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

				err = padp_ack(pconn, ack_header.flags, ack_header.size);

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
				pconn->palm_errno = PALMERR_ABORT;
				return -1;

			    case PADP_FRAGTYPE_NAK:
				/* No longer used. Silently ignore it. */
				goto mpretry;

			    default:
				/* XXX */
				fprintf(stderr,
					_("##### Unexpected packet type "
					  "%d.\n"),
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
					"got 0x%02x.\n"),
					pconn->padp.xid, pconn->slp.last_xid);
				pconn->palm_errno = PALMERR_ACKXID;
				return -1;
			}

			/* XXX - If it's not an ACK packet, assume that the
			 * ACK was sent and lost in transit, and that this
			 * is the response to the query we just sent.
			 */

			PADP_TRACE(3)
				fprintf(stderr, "padp_write: assuming lost ACK.\n");

			break;			/* Successfully got an ACK */
		}

		if (attempt >= PADP_MAX_RETRIES)
		{
			PADP_TRACE(3)
				fprintf(stderr,
					"PADP: Reached retry limit. "
					"Abandoning.\n");

			PConn_set_palmerrno(pconn, PALMERR_TIMEOUT);
			PConn_set_status(pconn, PCONNSTAT_LOST);
			return -1;
		}
		PADP_TRACE(9)
			fprintf(stderr, "Bottom of offset-loop\n");
	}
	PADP_TRACE(9)
		fprintf(stderr, "After offset-loop\n");

	return 0;
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
