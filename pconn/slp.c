/* slp.c
 *
 * Implementation of the Palm SLP (Serial Link Protocol)
 *
 * $Id: slp.c,v 1.1 1999-02-19 22:51:55 arensb Exp $
 */

#include <stdio.h>
#include <sys/types.h>	/* For read() */
#include <sys/uio.h>	/* For read() */
#include <unistd.h>	/* For read() */
#include <stdlib.h>	/* For malloc(), realloc() */
#include "palm_errno.h"
#include "slp.h"
#include "util.h"
#include "PConnection.h"

#define SLP_DEBUG	1
#ifdef SLP_DEBUG
int slp_debug = 0;

#define SLP_TRACE(level, format...)		\
	if (slp_debug >= (level))		\
		fprintf(stderr, "SLP:" format)

#endif	/* SLP_DEBUG */

/* slp_preamble
 * This is the preamble of every SLP packet. It never changes, so it
 * and its CRC are here to save time.
 */
static const ubyte slp_preamble[] = {
	0xbe, 0xef, 0xed,
};
static const ubyte slp_preamble_checksum = 0x9a;
	/* Sum of the preamble elements, mod 0xff */
static const uword slp_preamble_crc = 0xc9f1;	/* CRC of the preamble */

/* slp_init
 * Initialize a new PConnection.
 */
int
slp_init(struct PConnection *pconn)
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
slp_tini(struct PConnection *pconn)
{
	if (pconn == NULL)
		return 0;		/* Nothing to do */

	/* Free the input and output buffers. Set their length to 0,
	 * too, just on general principle.
	 */
	if (pconn->slp.inbuf != NULL)
		free(pconn->slp.inbuf);
	pconn->slp.inbuf_len = 0;

	if (pconn->slp.outbuf != NULL)
		free(pconn->slp.outbuf);
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
slp_bind(int fd, struct slp_addr *addr)
{
	struct PConnection *pconn;

	palm_errno = PALMERR_NOERR;

	/* Find the PConnection for this connection */
	if ((pconn = PConnLookup(fd)) == NULL)
	{
		fprintf(stderr, "Can't find a PConnection for fd %d\n", fd);
		palm_errno = PALMERR_BADF;
		return -1;
	}

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
slp_read(int fd,		/* File descriptor to read from */
	 const ubyte **buf,	/* Pointer to the data read */
	 uword *len)		/* Length of received message */
{
	int i;
	ssize_t err;		/* Length of read, and error code */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	struct PConnection *pconn;	/* The connection */
	struct slp_header header;	/* Parsed incoming header */
	ubyte checksum;		/* Packet checksum, for checking */
	uword got;		/* How many bytes we've read so far */
	uword want;		/* How many bytes we still want to read */
	bool ignore;		/* Are we ignoring this packet? */
	uword my_crc;		/* Computed CRC for the packet */

	palm_errno = PALMERR_NOERR;

	/* Get the PConnection from the file descriptor */
	if ((pconn = PConnLookup(fd)) == NULL)
	{
		fprintf(stderr, "slp_read: can't find PConnection for %d\n", fd);
		palm_errno = PALMERR_BADF;
		return -1;
	}

  redo:			/* I hope I may be forgiven for using gotos,
			 * but SLP's only response to errors of any
			 * kind is to drop the packet on the floor.
			 * Hence, if an error is received, it's
			 * easiest to just start again from the top.
			 */
	/* Read the preamble. */
	for (i = 0; i < sizeof(slp_preamble); i++)
	{
		err = read(pconn->fd, pconn->slp.header_inbuf+i, 1);
		if (err < 0)
		{
			perror("slp_read: read");
			palm_errno = PALMERR_SYSTEM;
			return err;
		}
		if (err == 0)
		{
			SLP_TRACE(10, "EOF in preamble\n");
			palm_errno = PALMERR_EOF;
			return 0;
		}
		if (pconn->slp.header_inbuf[i] != slp_preamble[i])
		{
			SLP_TRACE(5, "Got bogus character 0x%02x\n",
				  pconn->slp.header_inbuf[i]);
			goto redo;
		}
	}
/*  	SLP_TRACE(5, "Got a preamble\n"); */

	/* Read the header */
	want = SLP_HEADER_LEN;
	got = SLP_PREAMBLE_LEN;
	while (want > got)
	{
		err = read(pconn->fd, pconn->slp.header_inbuf+got, want-got);
		if (err < 0)
		{
			perror("slp_read: read");
			palm_errno = PALMERR_SYSTEM;
			return -1;
		}
		if (err == 0)
		{
			SLP_TRACE(10, "EOF in header\n");
			palm_errno = PALMERR_EOF;
			return 0;
		}
		got += err;
	}

#if SLP_DEBUG
	if (slp_debug >= 6)
		debug_dump(stderr, "SLP(h) <<<", pconn->slp.header_inbuf, got);
#endif	/* SLP_DEBUG */

	/* Parse the header */
	rptr = pconn->slp.header_inbuf+SLP_PREAMBLE_LEN;
	header.dest	= get_ubyte(&rptr);
	header.src	= get_ubyte(&rptr);
	header.type	= get_ubyte(&rptr);
	header.size	= get_uword(&rptr);
	header.xid	= get_ubyte(&rptr);
	header.checksum	= get_ubyte(&rptr);
	SLP_TRACE(5, "Got a header: %d->%d, type %d, size %d, xid 0x%02x, sum 0x%02x\n",
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
		fprintf(stderr, "slp_read: bad checksum: expected 0x%02x, got 0x%02x\n",
			checksum, header.checksum);
		goto redo;		/* Drop the packet on the floor */
	}
	SLP_TRACE(10, "Good checksum\n");

	/* See if we should ignore this packet */
	ignore = true;
	if ((header.type == pconn->slp.local_addr.protocol) &&
	    (header.dest == pconn->slp.local_addr.port))
		ignore = false;
	if (ignore)
		SLP_TRACE(6, "Ignoring packet\n");
	else
		SLP_TRACE(6, "Not ignoring packet\n");

	/* Before reading the body of the packet, see if the input buffer
	 * in the PConnection is big enough to hold it. If not, resize it.
	 */
	if (header.size > pconn->slp.inbuf_len)
	{
		ubyte *eptr;	/* Pointer to reallocated buffer */

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
	want = header.size;
	got = 0;
	while (want > got)
	{
		err = read(pconn->fd, pconn->slp.inbuf+got, want-got);
		if (err < 0)
		{
			perror("slp_read: read2");
			palm_errno = PALMERR_SYSTEM;
			return -1;
		}
		if (err == 0)
		{
			SLP_TRACE(10, "EOF in body\n");
			palm_errno = PALMERR_EOF;
			return 0;
		}
		got += err;
	}
#if SLP_DEBUG
	/* Dump the body, for debugging */
	if (slp_debug >= 5)
		debug_dump(stderr, "SLP(b) <<<", pconn->slp.inbuf, got);
#endif	/* SLP_DEBUG */

	/* Read the CRC */
	want = SLP_CRC_LEN;
	got = 0;
	while (want > got)
	{
		err = read(pconn->fd, pconn->slp.crc_inbuf+got, want-got);
		if (err < 0)
		{
			perror("slp_read: read");
			palm_errno = PALMERR_SYSTEM;
			return -1;
		}
		if (err == 0)
		{
			SLP_TRACE(10, "EOF in CRC\n");
			palm_errno = PALMERR_EOF;
			return 0;
		}
		got += err;
	}
#if SLP_DEBUG
	if (slp_debug >= 6)
		debug_dump(stderr, "SLP(c) <<<", pconn->slp.crc_inbuf, SLP_CRC_LEN);
#endif	/* SLP_DEBUG */
	SLP_TRACE(10, "Got CRC\n");

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
		fprintf(stderr, "SLP: bad CRC: expected 0x%04x, got 0x%04x\n",
			my_crc, peek_uword(rptr));
		goto redo;
	}
	SLP_TRACE(6, "Good CRC\n");

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
slp_write(int fd,
	  const ubyte *buf,
	  const uword len)
{
	int i;
	int err;
	struct PConnection *pconn;	/* The connection */
	static ubyte header_buf[SLP_HEADER_LEN];
				/* Buffer to hold the SLP header on output */
	static ubyte crc_buf[SLP_CRC_LEN];
				/* Buffer to hold the CRC on output */
	ubyte *wptr;		/* Pointer into buffers (for writing) */
	uword sent;		/* How many bytes have been sent so far */
	uword want;		/* How many bytes we still want to send */
	ubyte checksum;		/* Header checksum */
	uword crc;		/* Computed CRC of the packet */

	palm_errno = PALMERR_NOERR;

	/* Get the PConnection from the file descriptor */
	if ((pconn = PConnLookup(fd)) == NULL)
	{
		fprintf(stderr, "slp_write: can't find PConnection for %d\n", fd);
		palm_errno = PALMERR_BADF;
		return -1;
	}

	/* Build a packet header in 'header_buf' */
	wptr = header_buf;
	put_ubyte(&wptr, slp_preamble[0]);
	put_ubyte(&wptr, slp_preamble[1]);
	put_ubyte(&wptr, slp_preamble[2]);
	put_ubyte(&wptr, pconn->slp.remote_addr.port);	/* dest */
	put_ubyte(&wptr, pconn->slp.local_addr.port);	/* src */
	put_ubyte(&wptr, pconn->slp.local_addr.protocol);/* type */
	put_uword(&wptr, len);				/* size */
	put_ubyte(&wptr, pconn->padp.xid);		/* xid */
			/* XXX - It's unfortunate that the SLP layer
			 * has to reach into the PADP layer this way,
			 * but the SLP and PADP protocols are so
			 * tightly interwoven, I'm not sure there's a
			 * better way to do this.
			 */
	/* Compute the header checksum */
	checksum = 0;
	for (i = 0; i < SLP_HEADER_LEN-1; i++)
		checksum += header_buf[i];
	put_ubyte(&wptr, checksum);			/* checksum */

	/* Compute the CRC of the message */
	crc = crc16(header_buf, SLP_HEADER_LEN, 0);
	crc = crc16(buf, len, crc);

	/* Construct the CRC buffer */
	wptr = crc_buf;
	put_uword(&wptr, crc);

	/* Send the header */
	want = SLP_HEADER_LEN;
	sent = 0;
	while (sent < want)
	{
		err = write(pconn->fd, header_buf+sent, want-sent);
		if (err < 0)
		{
			perror("slp_write: write header");
			palm_errno = PALMERR_SYSTEM;
			return -1;
		}
		sent += err;
	}
#if SLP_DEBUG
	if (slp_debug >= 6)
		debug_dump(stderr, "SLP(h) >>>", header_buf, SLP_HEADER_LEN);
#endif	/* SLP_DEBUG */

	/* Send the body */
	want = len;
	sent = 0;
	while (sent < want)
	{
		err = write(pconn->fd, buf+sent, want-sent);
		if (err < 0)
		{
			perror("slp_write: write body");
			palm_errno = PALMERR_SYSTEM;
			return -1;
		}
		sent += err;
	}
#if SLP_DEBUG
	if (slp_debug >= 5)
		debug_dump(stderr, "SLP(b) >>>", buf, len);
#endif	/* SLP_DEBUG */

	/* Send the CRC */
	want = SLP_CRC_LEN;
	sent = 0;
	while (sent < want)
	{
		err = write(pconn->fd, crc_buf+sent, want-sent);
		if (err < 0)
		{
			perror("slp_write: write CRC");
			palm_errno = PALMERR_SYSTEM;
			return -1;
		}
		sent += err;
	}
#if SLP_DEBUG
	if (slp_debug >= 5)
		debug_dump(stderr, "SLP(c) >>>", crc_buf, SLP_CRC_LEN);
#endif	/* SLP_DEBUG */

	return len;		/* Success */
}
