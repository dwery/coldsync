/* slp.c
 *
 * Implementation of the Palm SLP (Serial Link Protocol)
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: slp.c,v 1.21 2001-11-05 00:26:57 arensb Exp $
 */

#include "config.h"
#include <stdio.h>
#include <sys/types.h>	/* For read() */
#include <sys/uio.h>	/* For read() */
#include <unistd.h>	/* For read() */
#include <stdlib.h>	/* For malloc(), realloc() */
#include <string.h>	/* For memset() */

#if HAVE_STRINGS_H
#  include <strings.h>	/* For bzero() */
#endif	/* HAVE_STRINGS_H */

#if HAVE_LIBINTL_H
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL_H */

#include "pconn/pconn.h"

int slp_trace = 0;		/* Debugging level for SLP stuff */

#define SLP_TRACE(n)	if (slp_trace >= (n))

/* slp_preamble
 * This is the preamble of every SLP packet.
 */
static const ubyte slp_preamble[] = {
	0xbe, 0xef, 0xed,
};

/* slp_init
 * Initialize a new PConnection.
 */
int
slp_init(PConnection *pconn)
{
	/* Allocate the initial input and output buffers */
	pconn->slp.inbuf = (ubyte *) malloc(SLP_INIT_INBUF_LEN);
	if (pconn->slp.inbuf == NULL)
	{
		/* Memory allocation failed */
		return -1;
	}
	pconn->slp.inbuf_len = SLP_INIT_INBUF_LEN;

	pconn->slp.outbuf = (ubyte *) malloc(SLP_INIT_OUTBUF_LEN);
	if (pconn->slp.outbuf == NULL)
	{
		/* Memory allocation failed */
		free(pconn->slp.inbuf);
		return -1;
	}
	pconn->slp.outbuf_len = SLP_INIT_OUTBUF_LEN;

	return 0;		/* Success */
}

/* slp_tini
 * Clean up a PConnection that's being closed.
 */
int
slp_tini(PConnection *pconn)
{
	if (pconn == NULL)
		return 0;		/* Nothing to do */

	/* Free the input and output buffers. Set their length to 0,
	 * too, just on general principle.
	 */
	if (pconn->slp.inbuf != NULL)
	{
		free(pconn->slp.inbuf);
		pconn->slp.inbuf = NULL;
	}
	pconn->slp.inbuf_len = 0;

	if (pconn->slp.outbuf != NULL)
	{
		free(pconn->slp.outbuf);
		pconn->slp.outbuf = NULL;
	}
	pconn->slp.outbuf_len = 0;

	return 0;
}

/* slp_bind
 * Bind a PConnection to an SLP address; that is, set up a port at
 * which this PConnection will listen, as well as the protocol
 * (typically PADP) that is listening. If any packets come in that do
 * not match the port or protocol, they'll be ignored. For now, this
 * is mainly to ignore the loopback packets that the Palm sends out
 * before a HotSync.
 */
int
slp_bind(PConnection *pconn, const struct slp_addr *addr)
{
	palm_errno = PALMERR_NOERR;

	/* Copy 'addr' to the "SLP local address" portion of the
	 * PConnection */
	pconn->slp.local_addr.protocol = addr->protocol;
	pconn->slp.local_addr.port = addr->port;

	return 0;
}

/* slp_read
 * Read a packet from the given file descriptor. A pointer to the
 * packet data (without the SLP header) is put in `*buf'. The length
 * of the data (not counting the SLP overhead) is put in `*len'.
 *
 * If successful, returns a positive value. On end-of-file returns 0
 * and sets 'palm_errno' to PALMERR_EOF. In case of error, returns a
 * negative value and sets 'palm_errno' to indicate the error.
 *
 * If slp_read() does not exit normally (i.e., either through EOF or
 * an error), the return values in `*buf' and `*len' are undefined and
 * should not be used.
 */
int
slp_read(PConnection *pconn,	/* Connection to Palm */
	 const ubyte **buf,	/* Pointer to the data read */
	 uword *len)		/* Length of received message */
{
	int i;
	ssize_t err;		/* Length of read, and error code */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	struct slp_header header;	/* Parsed incoming header */
	ubyte checksum;		/* Packet checksum, for checking */
	uword got;		/* How many bytes we've read so far */
	uword want;		/* How many bytes we still want to read */
	Bool ignore;		/* Are we ignoring this packet? */
	uword my_crc;		/* Computed CRC for the packet */

	palm_errno = PALMERR_NOERR;

  redo:			/* I hope I may be forgiven for using gotos,
			 * but SLP's only response to errors of any
			 * kind is to drop the packet on the floor.
			 * Hence, if an error is received, it's
			 * easiest to just start again from the top.
			 */
	/* Read the preamble. */
	for (i = 0; i < sizeof(slp_preamble); i++)
	{
		err = (*pconn->io_read)(pconn, pconn->slp.header_inbuf+i, 1);
		if (err < 0)
		{
			perror("slp_read: read");
			palm_errno = PALMERR_SYSTEM;
			return err;
		}
		if (err == 0)
		{
			SLP_TRACE(5)
				fprintf(stderr, "EOF in preamble\n");
			/* XXX - At this point, it would be nice to close
			 * the file descriptor pconn->fd to prevent others
			 * from writing to it, but that involves a whole
			 * lot of other changes, and doesn't really seem
			 * worth it.
			 */
			palm_errno = PALMERR_EOF;
			return 0;
		}
		if (pconn->slp.header_inbuf[i] != slp_preamble[i])
		{
			SLP_TRACE(5)
				fprintf(stderr, "Got bogus character 0x%02x\n",
					pconn->slp.header_inbuf[i]);
			goto redo;
		}
	}
	SLP_TRACE(6)
		fprintf(stderr, "Got a preamble\n");

	/* Read the header */
	want = SLP_HEADER_LEN;
	got = SLP_PREAMBLE_LEN;
	while (want > got)
	{
		err = (*pconn->io_read)(pconn,
					pconn->slp.header_inbuf+got, want-got);
		if (err < 0)
		{
			perror("slp_read: read");
			palm_errno = PALMERR_SYSTEM;
			return -1;
		}
		if (err == 0)
		{
			SLP_TRACE(5)
				fprintf(stderr, "EOF in header\n");
			palm_errno = PALMERR_EOF;
			return 0;
		}
		got += err;
	}

	SLP_TRACE(6)
		debug_dump(stderr, "SLP(h) <<<", pconn->slp.header_inbuf, got);

	/* Parse the header */
	rptr = pconn->slp.header_inbuf+SLP_PREAMBLE_LEN;
	header.dest	= get_ubyte(&rptr);
	header.src	= get_ubyte(&rptr);
	header.type	= get_ubyte(&rptr);
	header.size	= get_uword(&rptr);
	header.xid	= get_ubyte(&rptr);
	header.checksum	= get_ubyte(&rptr);
	SLP_TRACE(5)
		fprintf(stderr,
			"Got a header: %d->%d, "
			"type %d, size %d, xid 0x%02x, sum 0x%02x\n",
			header.src,
			header.dest,
			header.type,
			header.size,
			header.xid,
			header.checksum);

	/* Put the remote address implied by the packet in the PConnection,
	 * so we know whom to reply to.
	 */
	/* XXX - This really ought to coordinate with an appropriate
	 * accept() call.
	 */
	pconn->slp.remote_addr.protocol = header.type;
	pconn->slp.remote_addr.port = header.src;

	/* Make sure the checksum is good */
	checksum = 0;
	/* Sum up everything except for the checksum byte */
	for (i = 0; i < SLP_HEADER_LEN-1; i++)
		checksum += pconn->slp.header_inbuf[i];

	if (checksum != header.checksum)
	{
		fprintf(stderr, _("%s: bad checksum: expected 0x%02x, "
				  "got 0x%02x.\n"),
			"slp_read",
			checksum, header.checksum);
		goto redo;		/* Drop the packet on the floor */
	}
	SLP_TRACE(6)
		fprintf(stderr, "Good checksum\n");

	/* See if we should ignore this packet */
	ignore = True;
	if ((header.type == pconn->slp.local_addr.protocol) &&
	    (header.dest == pconn->slp.local_addr.port))
		ignore = False;
	if (ignore)
	{
		SLP_TRACE(6)
			fprintf(stderr, "Ignoring packet\n");
	} else {
		SLP_TRACE(6)
			fprintf(stderr, "Not ignoring packet\n");
	}

	/* Before reading the body of the packet, see if the input buffer
	 * in the PConnection is big enough to hold it. If not, resize it.
	 */
	if (header.size > pconn->slp.inbuf_len)
	{
		ubyte *eptr;	/* Pointer to reallocated buffer */

		SLP_TRACE(6)
			fprintf(stderr,
				"Resizing SLP input buffer from %ld to %d\n",
				pconn->slp.inbuf_len,
				header.size);

		/* Reallocate the input buffer. We use the temporary
		 * variable `eptr' in case realloc() fails: we don't
		 * want to lose the existing buffer.
		 */
		eptr = (ubyte *) realloc(pconn->slp.inbuf, header.size);
		if (eptr == NULL)
		{
			/* Reallocation failed */
			palm_errno = PALMERR_NOMEM;
			return -1;
		}
		pconn->slp.inbuf = eptr;	/* Set the new buffer */
		pconn->slp.inbuf_len = header.size;
						/* and record its new size */
	}

	/* Read the body */
	bzero((void *) pconn->slp.inbuf, pconn->slp.inbuf_len);
					/* Clear out the remains of the
					 * last packet read.
					 */
	want = header.size;
	got = 0;
	while (want > got)
	{
		err = (*pconn->io_read)(pconn, pconn->slp.inbuf+got, want-got);
		if (err < 0)
		{
			perror("slp_read: read2");
			palm_errno = PALMERR_SYSTEM;
			return -1;
		}
		if (err == 0)
		{
			SLP_TRACE(5)
				fprintf(stderr, "EOF in body\n");
			palm_errno = PALMERR_EOF;
			return 0;
		}

		SLP_TRACE(8)
		{
			/* Dump just this chunk */
			fprintf(stderr, "Read SLP chunk:\n");
			debug_dump(stderr, "SLP <<< ",
				   pconn->slp.inbuf+got,
				   err);
		}	

		got += err;
	}
	/* Dump the body, for debugging */
	SLP_TRACE(6)
		debug_dump(stderr, "SLP(b) <<<", pconn->slp.inbuf, got);

	/* Read the CRC */
	want = SLP_CRC_LEN;
	got = 0;
	while (want > got)
	{
		err = (*pconn->io_read)(pconn,
					pconn->slp.crc_inbuf+got, want-got);
		if (err < 0)
		{
			perror("slp_read: read");
			palm_errno = PALMERR_SYSTEM;
			return -1;
		}
		if (err == 0)
		{
			SLP_TRACE(5)
				fprintf(stderr, "EOF in CRC\n");
			palm_errno = PALMERR_EOF;
			return 0;
		}
		got += err;
	}
	SLP_TRACE(6)
		debug_dump(stderr, "SLP(c) <<<", pconn->slp.crc_inbuf,
			   SLP_CRC_LEN);
	SLP_TRACE(5)
		fprintf(stderr, "Got CRC\n");

	/* If this packet is being ignored, just go back up to the top
	 * and listen for the next packet.
	 */
	if (ignore)
		goto redo;

	/* If we've gotten this far, we must want this packet. Make
	 * sure it has a good CRC. This is done in three steps, one
	 * each for the header, body and CRC buffers, each step using
	 * the previous step's output as its own starting CRC, thus
	 * obtaining the CRC of the entire SLP packet.
	 *
	 * The test relies on a property of CRCs: if CRC(s1, s2, ...
	 * sN) == c1, c2, then CRC(s1, s2, ... sN, c1, c2) == 0.
	 */
	my_crc = crc16(pconn->slp.header_inbuf, SLP_HEADER_LEN, 0);
	my_crc = crc16(pconn->slp.inbuf, header.size, my_crc);
	my_crc = crc16(pconn->slp.crc_inbuf, SLP_CRC_LEN, my_crc);
	if (my_crc != 0)
	{
		rptr = pconn->slp.crc_inbuf;
		fprintf(stderr,
			_("SLP: bad CRC: expected 0x%04x, got 0x%04x.\n"),
			my_crc, peek_uword(rptr));
		goto redo;
	}
	SLP_TRACE(6)
		fprintf(stderr, "Good CRC\n");

	pconn->slp.last_xid = header.xid;
				/* Set the transaction ID so the next
				 * protocol up knows it.
				 */
	*buf = pconn->slp.inbuf;	/* Tell the caller where to find the
					 * data */
	*len = header.size;		/* and how much of it there was */
	return 1;		/* Success */
}

/* slp_write
 * Write a SLP packet on the given file descriptor, with contents
 * 'buf' and length 'len'.
 * Returns the number of bytes written (excluding SLP overhead)
 */
int
slp_write(PConnection *pconn,
	  const ubyte *buf,
	  const uword len)
{
	int i;
	int err;
	ubyte *wptr;		/* Pointer into buffers (for writing) */
	uword sent;		/* How many bytes have been sent so far */
	uword want;		/* How many bytes we still want to send */
	ubyte checksum;		/* Header checksum */
	uword crc;		/* Computed CRC of the packet */

	palm_errno = PALMERR_NOERR;
	SLP_TRACE(5)
		fprintf(stderr, "slp_write(x, x, %d)\n", len);

	/* Make sure the output buffer is big enough to hold this packet
	 * and the SLP overhead.
	 */
	if (pconn->slp.outbuf_len < SLP_HEADER_LEN + len + SLP_CRC_LEN)
	{
		ubyte *eptr;	/* Pointer to reallocated buffer */

		SLP_TRACE(6)
			fprintf(stderr,
				"Resizing SLP output buffer from %ld to %d\n",
				pconn->slp.outbuf_len,
				SLP_HEADER_LEN + len + SLP_CRC_LEN);

		/* Reallocate the input buffer. We use the temporary
		 * variable `eptr' in case realloc() fails: we don't
		 * want to lose the existing buffer.
		 */
		eptr = (ubyte *) realloc(pconn->slp.outbuf,
					 SLP_HEADER_LEN + len + SLP_CRC_LEN);
		if (eptr == NULL)
		{
			/* Reallocation failed */
			palm_errno = PALMERR_NOMEM;
			return -1;
		}
		pconn->slp.outbuf = eptr;	/* Set the new buffer */
		pconn->slp.outbuf_len = SLP_HEADER_LEN + len + SLP_CRC_LEN;
						/* and record its new size */
	}

	/* Build a packet header in 'header_buf' */
	wptr = pconn->slp.outbuf;
	put_ubyte(&wptr, slp_preamble[0]);
	put_ubyte(&wptr, slp_preamble[1]);
	put_ubyte(&wptr, slp_preamble[2]);
	put_ubyte(&wptr, pconn->slp.remote_addr.port);	/* dest */
	put_ubyte(&wptr, pconn->slp.local_addr.port);	/* src */
	put_ubyte(&wptr, pconn->slp.local_addr.protocol);/* type */
	put_uword(&wptr, len);				/* size */
	put_ubyte(&wptr, pconn->padp.xid);		/* xid */
			/* It's unfortunate that the SLP layer has to reach
			 * into the PADP layer this way, but the SLP and
			 * PADP protocols are so tightly interwoven, I'm
			 * not sure there's a better way to do this.
			 */
	/* Compute the header checksum */
	checksum = 0;
	for (i = 0; i < SLP_HEADER_LEN-1; i++)
		checksum += pconn->slp.outbuf[i];
	put_ubyte(&wptr, checksum);			/* checksum */

	/* Append the body of the packet to the buffer */
	memcpy(pconn->slp.outbuf + SLP_HEADER_LEN, buf, len);

	/* Compute the CRC of the message */
	crc = crc16(pconn->slp.outbuf, SLP_HEADER_LEN + len, 0);

	/* Construct the CRC buffer */
	wptr += len;
	put_uword(&wptr, crc);

	/* Send the SLP packet */
	want = SLP_HEADER_LEN + len + SLP_CRC_LEN;
	sent = 0;
	while (sent < want)
	{
		err = (*pconn->io_write)(pconn, pconn->slp.outbuf+sent,
					 want-sent);
		if (err < 0)
		{
			perror("slp_write: write");
			palm_errno = PALMERR_SYSTEM;
			/* XXX - At this point, it would be nice to close
			 * the file descriptor pconn->fd to prevent others
			 * from writing to it, but that involves a whole
			 * lot of other changes, and doesn't really seem
			 * worth it.
			 */
			return -1;
		}
		sent += err;
	}
	SLP_TRACE(6)
	{
		debug_dump(stderr, "SLP(h) >>>", pconn->slp.outbuf,
			   SLP_HEADER_LEN);
		debug_dump(stderr, "SLP(b) >>>",
			   pconn->slp.outbuf + SLP_HEADER_LEN,
			   len);
		debug_dump(stderr, "SLP(c) >>>",
			   pconn->slp.outbuf + SLP_HEADER_LEN + len,
			   SLP_CRC_LEN);
	}

	return len;		/* Success */
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
