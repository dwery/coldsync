/* slp.c
 *
 * Implementation of the Palm SLP (Serial Link Protocol)
 *
 * $Id: slp.c,v 1.3 1999-01-31 22:16:30 arensb Exp $
 */

#include <stdio.h>
#include <sys/types.h>	/* For read() */
#include <sys/uio.h>	/* For read() */
#include <unistd.h>	/* For read() */
#include <string.h>	/* For memcpy() */
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

int slp_errno;			/* Error code */

char *slp_errlist[] = {
	"No error",				/* SLPERR_NOERR */
	"End of file",				/* SLPERR_EOF */
	"Bad connection/file descriptor",	/* SLPERR_BADFD */
	"System call error",			/* SLPERR_SEP */
	"Input buffer too small",		/* SLPERR_ISIZE */
};

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

#if 0
/* XXX - bogus name */
static uword
palm_crc(uword sofar, uword len, const ubyte *data)
{
	/* XXX - Just insert the code here */
	extern uword Crc16CalcBlock(const ubyte *data, uword len, uword sofar);

	return Crc16CalcBlock(data, len, sofar);
}

/* slp_recv
 * Receive a SLP packet for protocol 'protocol'.
 */
int
slp_recv(int/*PConnHandle*/ ph, ubyte *buf, uword len,
	 struct slp_header *header)
{
	int i;
	int err;
	ubyte c;		/* Preamble will be read into here */
	static ubyte header_buf[7];
				/* Buffer into which to read the
				 * header */
	static ubyte body_buf[SLP_MAX_BODY_LEN];
				/* Buffer into which to read the body */
				/* XXX - This is arbitrarily limited */
	static ubyte crc_buf[2];
				/* Buffer into which to read the CRC */
	int want;		/* How many bytes we still want to read */
	int got;		/* How many bytes we've got so far */
	ubyte checksum;		/* Checksum calculated from the
				 * received data */
	uword got_crc;		/* CRC received in packet */
	uword my_crc;		/* CRC calculated from packet data */

  redo:		/* May the programming gods forgive me for using
		 * gotos, but SLP is a protocol whose error-control
		 * mechanism consists of dropping the offending packet
		 * on the floor. As such, the easiest thing to do is
		 * to just restart from the top.
		 */
	/* Read the preamble */

	/* First byte */
	err = read(ph->fd, &c, 1);
	if (err < 0)
	{
		perror("slp_recv: read1");
		return -1;
	}
if (err == 0)
fprintf(stderr, "read() returned 0 at 1\n");
	if (c != 0xbe)
	{
		SLP_TRACE(1, "read1: got unexpected value 0x%02x\n", c);
		goto redo;
	}
	SLP_TRACE(3, "slp_recv: got 0xbe\n");

	/* Second byte */
	err = read(ph->fd, &c, 1);
	if (err < 0)
	{
		perror("slp_read: read2");
		return -1;
	}
if (err == 0)
fprintf(stderr, "read() returned 0 at 2\n");
	if (c != 0xef)
	{
		SLP_TRACE(1, "read2: got unexpected value 0x%02x\n",
			  c);
		goto redo;
	}
	SLP_TRACE(3, "slp_recv: got 0xef\n");

	/* Third byte */
	err = read(ph->fd, &c, 1);
	if (err < 0)
	{
		perror("slp_read: read3");
		return -1;
	}
if (err == 0)
fprintf(stderr, "read() returned 0 at 3\n");
	if (c != 0xed)
	{
		SLP_TRACE(1, "read3: got unexpected value 0x%02x\n",
			  c);
		goto redo;
	}
	SLP_TRACE(3, "Got a preamble\n");

	/* Read the header */
	want = 7;
	got = 0;
	while (got < want)
	{
		err = read(ph->fd, &header_buf[got], want-got);
		if (err < 0)
		{
			perror("slp_read: read header");
			slp_errno = SLPERR_SEP;
			return -1;
		}
if (err == 0)
fprintf(stderr, "read() returned 0 at 4\n");
		got += err;
	}
	/* Dissect 'header_buf' and put the parts in '*header' */
	header->dest = header_buf[0];
	header->src = header_buf[1];
	header->type = header_buf[2];
	header->size = ((uword) header_buf[3] << 8) |
		header_buf[4];
	header->xid = header_buf[5];
	header->checksum = header_buf[6];

	/* Show the contents of the header */
	SLP_TRACE(1, "<   SLP: %d->%d, type %d, size %d, ID 0x%02x, sum 0x%02x\n",
		  header->src,
		  header->dest,
		  header->type,
		  header->size,
		  header->xid,
		  header->checksum);

	/* Make sure the checksum is good */
	checksum = preamble_checksum;
	for (i = 0; i < sizeof(header_buf)-1; i++)
		checksum += header_buf[i];
	if (checksum != header->checksum)
	{
		fprintf(stderr, "SLP: bad checksum: expected %d, got %d\n",
			checksum, header->checksum);
		goto redo;
	}

	/* Read the body */
	want = header->size;
	got = 0;
	while (got < want)
	{
		err = read(ph->fd, &body_buf[got], want-got);
		if (err < 0)
		{
			perror("slp_read: read body");
			return -1;
		}
if (err == 0)
fprintf(stderr, "read() returned 0 at 5\n");
		got += err; 
	}
#ifdef SLP_DEBUG
	if (slp_debug >= 2)
		/* Dump the body, for debugging */
		if (header->size > 0)
		{
			for (i = 0; i < header->size; i++)
			{
				fprintf(stderr, "%02x ", body_buf[i]);
				if ((i % 16) == 15)
					fprintf(stderr, "\n");
			}
			if ((i % 16) != 0)
				fprintf(stderr, "\n");
		}
#endif	/* SLP_DEBUG */

	/* Read the CRC */
	want = 2;
	got = 0;
	while (got < want)
	{
		err = read(ph->fd, &crc_buf[got], want-got);
		if (err < 0)
		{
			perror("slp_read: read CRC");
			return -1;
		}
if (err == 0)
fprintf(stderr, "read() returned 0 at 6\n");
		got += err; 
	}

	/* Check the CRC */
	got_crc = ((uword) crc_buf[0] << 8) | crc_buf[1];
	my_crc = preamble_crc;
	my_crc = palm_crc(my_crc, sizeof(header_buf), header_buf);
	my_crc = palm_crc(my_crc, header->size, body_buf);
	if (got_crc != my_crc)
	{
		fprintf(stderr, "CRC mismatch: expected 0x%04x, got 0x%04x\n",
			my_crc, got_crc);
		goto redo;
	}
SLP_TRACE(2, "Good CRC\n");
if (header->type == slpp_loopback)
goto redo;

	/* Now make sure this is the kind of packet the caller wanted */
#if 0
	if (header->type != control->type)
	{
		SLP_TRACE(2, "SLP mismatch: wanted type %d, got %d\n",
			  control->type, header->type);
		goto redo;
	}
#else
	if (header->type == slpp_loopback)
	{
		SLP_TRACE(2, "SLP: ignoring loopback packet\n");
		goto redo;
	}
#endif

	/* Check the size */
	if (len < header->size)
	{
		fprintf(stderr, "SLP: buffer too small for packet (%d can't fit in %d)\n",
			header->size, len);
		return -1;
	}

	/* Everything looks fine. */
	memcpy(buf, body_buf, header->size);
	return header->size;
}

int
slp_send(int/*PConnHandle*/ ph, ubyte *buf, uword len,
	struct slp_header *header)
{
#if 0
	int i;
	int err;
	static ubyte outbuf[10];	/* Buffer for preamble and
					 * header, and later CRC. */
	ubyte checksum;
	uword my_crc;

	header->size = len;

	/* Calculate the checksum */
	checksum = preamble_checksum +
		header->dst +
		header->src +
		header->type +
		((header->size >> 8) & 0xff) +
		(header->size & 0xff);

	/* Create a header in 'outbuf' */
	memcpy(outbuf, preamble, sizeof(preamble));
	/* XXX - Perhaps the source and destination ought to be
	 * swapped here? */
	outbuf[3] = header->dst;
	outbuf[4] = header->src;
	outbuf[5] = header->type;
	outbuf[6] = (header->size >> 8) & 0xff;
	outbuf[7] = header->size & 0xff;
	outbuf[8] = checksum;

	/* Calculate the CRC now, since it's handy */
	my_crc = palm_crc/*slp_crc*/(preamble_crc, 9, outbuf);
	my_crc = palm_crc/*slp_crc*/(my_crc, len, buf);

	/* Show the contents of the header */
	SLP_TRACE(1, ">   SLP: %d->%d, type %d, size %d, ID 0x%02x, sum 0x%02x\n",
		  header->src,
		  header->dst,
		  header->type,
		  header->size,
		  header->xid,
		  /*header->*/checksum);

#ifdef SLP_DEBUG
	if (slp_debug >= 2)
	{
		for (i = 0; i < 9; i++)
		{
			fprintf(stderr, "%02x ", outbuf[i]);
		}
		fprintf(stderr, "\n");
	}
#endif	/* SLP_DEBUG */

	/* Send the SLP header */
	err = write(ph->fd, outbuf, 9);
SLP_TRACE(2, "wrote %d/%d bytes\n", err, 9);
	if (err != 9)
	{
		perror("slp_send (header): write");
		return -1;
	}

#ifdef SLP_DEBUG
	if (slp_debug >= 2)
	{
		for (i = 0; i < len; i++)
		{
			fprintf(stderr, "%02x ", buf[i]);
			if ((i % 16) == 15)
				fprintf(stderr, "\n");
		}
		if ((i % 16) != 0)
			fprintf(stderr, "\n");
	}
#endif	/* SLP_DEBUG */

	/* Now send the body */
	err = write(ph->fd, buf, len);
SLP_TRACE(2, "wrote %d/%d bytes\n", err, len);
	if (err != len)
	{
		perror("slp_send (body): write");
		return -1;
	}

	/* Now send the CRC */
	outbuf[0] = (my_crc >> 8) & 0xff;
	outbuf[1] = my_crc & 0xff;

	SLP_TRACE(2, "%02x %02x\n", outbuf[0], outbuf[1]);

	err = write(ph->fd, outbuf, 2);
SLP_TRACE(2, "wrote %d/%d bytes\n", err, 2);
	if (err != 2)
	{
		perror("slp_send (crc): write");
		return -1;
	}

	return 0;
#endif	/* 0 */
	int i;
	int err;
	static ubyte outbuf[7+SLP_MAX_BODY_LEN+2];
	ubyte *pos;
	ubyte checksum;
	uword my_crc;
header->size = len;

#ifdef SLP_DEBUG
if (slp_debug >= 1)
{
	if (header->size != len)
		fprintf(stderr, "*** Warning: header->size == %d, len == %d\n",
			header->size, len);
}
#endif	/* SLP_DEBUG */
	/* Construct an outgoing packet */
	pos = &outbuf[0];
	memcpy(pos, preamble, sizeof(preamble));
	pos += sizeof(preamble);
	*pos++ = header->src;	/* These two are reversed */
	*pos++ = header->dest;
	*pos++ = header->type;
	*pos++ = (header->size >> 8) & 0xff;
	*pos++ = header->size & 0xff;
	*pos++ = header->xid;

	/* Calculate checksum */
	checksum = 0;
	for (i = 0; i < (pos - outbuf); i++)
		checksum += outbuf[i];
	*pos++ = checksum;

	/* Copy the caller's message to 'outbuf' */
	memcpy(pos, buf, len);
	pos += len;

	my_crc = palm_crc/*slp_crc*/(0, len+10/*pos-outbuf*/, outbuf);
	*pos++ = (my_crc >> 8) & 0xff;
	*pos++ = my_crc & 0xff;
#ifdef SLP_DEBUG
{
	struct slp_header header;

	/* Dissect 'header_buf' and put the parts in '*header' */
	header.dest = outbuf[3];
	header.src = outbuf[4];
	header.type = outbuf[5];
	header.size = ((uword) outbuf[6] << 8) |
		outbuf[7];
	header.xid = outbuf[8];
	header.checksum = outbuf[9];

	/* Show the contents of the header */
	SLP_TRACE(1, ">   SLP: %d->%d, type %d, size %d, ID 0x%02x, sum 0x%02x\n",
		header.src,
		header.dest,
		header.type,
		header.size,
		header.xid,
		header.checksum);

	/* Make sure the checksum is good */
	checksum = 0;
	for (i = 0; i < 9; i++)
		checksum += outbuf[i];
	if (checksum != header.checksum)
	{
		fprintf(stderr, "SLP: bad checksum: expected %d, got %d\n",
			checksum, header.checksum);
	}

	if (slp_debug >= 2)
	{
		/* Dump the body, for debugging */
		if (len > 0)
		{
			for (i = 0; i < 10+len+2; i++)
			{
				fprintf(stderr, "%02x ", outbuf[i]);
				if ((i % 16) == 15)
					fprintf(stderr, "\n");
			}
			if ((i % 16) != 0)
				fprintf(stderr, "\n");
		}
	}
}
#endif	/* SLP_DEBUG */

	err = write(ph->fd, outbuf, 10+len+2);
	SLP_TRACE(2, "wrote %d/%d bytes\n", err, 10+len+2);
	if (err < 0)
	{
		perror("slp_send: write");
		return -1;
	}
	return err;
}
#endif	/* 0 */

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

	slp_errno = SLPERR_NOERR;

	/* Find the PConnection for this connection */
	if ((pconn = PConnLookup(fd)) == NULL)
	{
		fprintf(stderr, "Can't find a PConnection for fd %d\n", fd);
		slp_errno = SLPERR_BADFD;
		return -1;
	}

	/* Copy 'addr' to the "SLP local address" portion of the
	 * PConnection */
	pconn->slp.local_addr.protocol = addr->protocol;
	pconn->slp.local_addr.port = addr->port;

	return 0;
}

/* slp_read
 * Read a packet from the given file descriptor and put the data read
 * (minus the SLP protocol stuff) into the buffer 'buf'. No more than
 * 'len' characters will be written to 'buf'.
 *
 * If successful, returns the number of data bytes read (i.e., not
 * counting the SLP overhead). On end-of-file returns 0 and sets
 * 'slp_errno' to SLPERR_EOF. In case of error, returns -1 and sets
 * 'slp_errno' to indicate the error.
 *
 * If slp_read() does not exit normally (either through EOF or an
 * error), the contents of the data buffer ('buf') is undetermined.
 */
int
slp_read(int fd,		/* File descriptor to read from */
	 ubyte *buf,		/* Buffer to put the data into */
	 uword len)		/* Max # ubytes to read */
{
	int i;
	int err;
	static ubyte header_buf[SLP_HEADER_LEN];
	static ubyte crc_buf[SLP_CRC_LEN];
	ubyte *ptr;		/* Pointer into various buffers */
	struct PConnection *pconn;	/* The connection */
	struct slp_header header;	/* Parsed incoming header */
	ubyte checksum;		/* Packet checksum, for checking */
	uword got;		/* How many bytes we've read so far */
	uword want;		/* How many bytes we still want to read */
	bool ignore;		/* Are we ignoring this packet? */
	uword my_crc;		/* Computed CRC for the packet */

	slp_errno = SLPERR_NOERR;

	/* Get the PConnection from the file descriptor */
	if ((pconn = PConnLookup(fd)) == NULL)
	{
		fprintf(stderr, "slp_read: can't find PConnection for %d\n", fd);
		slp_errno = SLPERR_BADFD;
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
		err = read(pconn->fd, header_buf+i, 1);
		if (err < 0)
		{
			perror("slp_read: read");
			slp_errno = SLPERR_SEP;
			return err;
		}
		if (err == 0)
		{
			SLP_TRACE(10, "EOF in preamble\n");
			slp_errno = SLPERR_EOF;
			return 0;
		}
		if (header_buf[i] != slp_preamble[i])
		{
			SLP_TRACE(5, "Got bogus character 0x%02x\n",
				  header_buf[i]);
			goto redo;
		}
	}
	SLP_TRACE(5, "Got a preamble\n");

	/* Read the header */
	want = SLP_HEADER_LEN;
	got = SLP_PREAMBLE_LEN;
	while (want > got)
	{
		err = read(pconn->fd, header_buf+got, want-got);
		if (err < 0)
		{
			perror("slp_read: read2");
			slp_errno = SLPERR_SEP;
			return -1;
		}
		if (err == 0)
		{
			SLP_TRACE(10, "EOF in header\n");
			slp_errno = SLPERR_EOF;
			return 0;
		}
		got += err;
	}

#if SLP_DEBUG
	if (slp_debug >= 6)
		debug_dump("SLP(h) <<<", header_buf, got);
#endif	/* SLP_DEBUG */

	/* Parse the header */
	ptr = header_buf+SLP_PREAMBLE_LEN;
	header.dest	= get_ubyte(&ptr);
	header.src	= get_ubyte(&ptr);
	header.type	= get_ubyte(&ptr);
	header.size	= get_uword(&ptr);
	header.xid	= get_ubyte(&ptr);
	header.checksum	= get_ubyte(&ptr);
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
		checksum += header_buf[i];

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

	/* Read the body. */
	if (ignore)
	{
		/* This packet is being ignored; just read it and toss
		 * the results.
		 */
		want = header.size;
		got = 0;

		/* We have a buffer handy, so might as well use that */
		while (want > 0)
		{
			if (want > len)
				err = read(pconn->fd, buf, len);
			else
				err = read(pconn->fd, buf, want);
			if (err < 0)
			{
				perror("slp_read: read 3");
				slp_errno = SLPERR_SEP;
				return -1;
			}
			if (err == 0)
			{
				SLP_TRACE(10, "EOF in ignored body\n");
				slp_errno = SLPERR_EOF;
				return 0;
			}
#if SLP_DEBUG
		/* Dump the body, for debugging */
		if (slp_debug >= 6)
			debug_dump("SLP(i) <<<", buf, err);
#endif	/* SLP_DEBUG */
			got += err;
			want -= err;
		}
	} else {
		/* This packet is not being ignored. Read it for real.
		 */

		/* Make sure the buffer has enough room to read this
		 * packet.
		 */
		if (header.size > len)
		{
			fprintf(stderr, "slp_read: 'len' not big enough for packet (%d bytes can't fit in %d)\n",
				header.size, len);
			slp_errno = SLPERR_ISIZE;
			return -1;
		}

		/* Read the body */
		want = header.size;
		got = 0;
		while (want > got)
		{
			err = read(pconn->fd, buf+got, want-got);
			if (err < 0)
			{
				perror("slp_read: read2");
				slp_errno = SLPERR_SEP;
				return -1;
			}
			if (err == 0)
			{
				SLP_TRACE(10, "EOF in body\n");
				slp_errno = SLPERR_EOF;
				return 0;
			}
			got += err;
		}
#if SLP_DEBUG
		/* Dump the body, for debugging */
		if (slp_debug >= 5)
			debug_dump("SLP(b) <<<", buf, got);
#endif	/* SLP_DEBUG */
	}

	/* Read the CRC */
	want = SLP_CRC_LEN;
	got = 0;
	while (want > got)
	{
		err = read(pconn->fd, crc_buf+got, want-got);
		if (err < 0)
		{
			perror("slp_read: read4");
			slp_errno = SLPERR_SEP;
			return -1;
		}
		if (err == 0)
		{
			SLP_TRACE(10, "EOF in CRC\n");
			slp_errno = SLPERR_EOF;
			return 0;
		}
		got += err;
	}
#if SLP_DEBUG
	if (slp_debug >= 6)
		debug_dump("SLP(c) <<<", crc_buf, SLP_CRC_LEN);
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
	 * The test relies on a property of CRCs: if CRC(s1s2...sN) ==
	 * c1c2, then CRC(s1s2...sNc1c2) == 0.
	 */
	my_crc = crc16(header_buf, SLP_HEADER_LEN, 0);
	my_crc = crc16(buf, header.size, my_crc);
	my_crc = crc16(crc_buf, SLP_CRC_LEN, my_crc);
	if (my_crc != 0)
	{
		ptr = crc_buf;
		fprintf(stderr, "SLP: bad CRC: expected 0x%04x, got 0x%04x\n",
			my_crc, peek_uword(ptr));
		goto redo;
	}
	SLP_TRACE(6, "Good CRC\n");

	pconn->slp.last_xid = header.xid;
				/* Set the transaction ID so the next
				 * protocol up knows it.
				 */
	return header.size;
}

/* slp_write
 * Write a SLP packet on the given file descriptor, with contents
 * 'buf' and length 'len'.

 * Returns the number of bytes written (excluding SLP overhead)
 */
int
slp_write(int fd,
	  ubyte *buf,
	  uword len)
{
	int i;
	int err;
	struct PConnection *pconn;	/* The connection */
	static ubyte header_buf[SLP_HEADER_LEN];
				/* Buffer to hold the SLP header on output */
	static ubyte crc_buf[SLP_CRC_LEN];
				/* Buffer to hold the CRC on output */
	ubyte *ptr;		/* Pointer into buffers */
	uword sent;		/* How many bytes have been sent so far */
	uword want;		/* How many bytes we still want to send */
	ubyte checksum;		/* Header checksum */
	uword crc;		/* Computed CRC of the packet */

	slp_errno = SLPERR_NOERR;

	/* Get the PConnection from the file descriptor */
	if ((pconn = PConnLookup(fd)) == NULL)
	{
		fprintf(stderr, "slp_write: can't find PConnection for %d\n", fd);
		slp_errno = SLPERR_BADFD;
		return -1;
	}

	/* Build a packet header in 'header_buf' */
	ptr = header_buf;
	put_ubyte(&ptr, slp_preamble[0]);
	put_ubyte(&ptr, slp_preamble[1]);
	put_ubyte(&ptr, slp_preamble[2]);
	put_ubyte(&ptr, pconn->slp.remote_addr.port);	/* dest */
	put_ubyte(&ptr, pconn->slp.local_addr.port);	/* src */
	put_ubyte(&ptr, pconn->slp.local_addr.protocol);/* type */
	put_uword(&ptr, len);/* size */
	put_ubyte(&ptr, pconn->padp.xid);		/* xid */
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
	put_ubyte(&ptr, checksum);			/* checksum */

	/* Compute the CRC of the message */
	crc = crc16(header_buf, SLP_HEADER_LEN, 0);
	crc = crc16(buf, len, crc);

	/* Construct the CRC buffer */
	ptr = crc_buf;
	put_uword(&ptr, crc);

	/* Send the header */
	want = SLP_HEADER_LEN;
	sent = 0;
	while (sent < want)
	{
		err = write(pconn->fd, header_buf+sent, want-sent);
		if (err < 0)
		{
			perror("slp_write: write header");
			slp_errno = SLPERR_SEP;
			return -1;
		}
		sent += err;
	}
#if SLP_DEBUG
	if (slp_debug >= 6)
		debug_dump("SLP(h) >>>", header_buf, SLP_HEADER_LEN);
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
			slp_errno = SLPERR_SEP;
			return -1;
		}
		sent += err;
	}
#if SLP_DEBUG
	if (slp_debug >= 5)
		debug_dump("SLP(b) >>>", buf, len);
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
			slp_errno = SLPERR_SEP;
			return -1;
		}
		sent += err;
	}
#if SLP_DEBUG
	if (slp_debug >= 5)
		debug_dump("SLP(c) >>>", crc_buf, SLP_CRC_LEN);
#endif	/* SLP_DEBUG */

	return len;		/* Success */
}
