/* slp.c
 *
 * Implementation of the Palm SLP (Serial Link Protocol)
 *
 * $Id: slp.c,v 1.1 1999-01-22 18:13:12 arensb Exp $
 */

#include "slp.h"
#include <stdio.h>
#include <sys/types.h>	/* For read() */
#include <sys/uio.h>	/* For read() */
#include <unistd.h>	/* For read() */
#include <string.h>	/* For memcpy() */

#define SLP_DEBUG	1
#ifdef SLP_DEBUG
int slp_debug = 0;

#define SLP_TRACE(level, format...)		\
	if (slp_debug >= (level))		\
		fprintf(stderr, format);

#endif	/* SLP_DEBUG */

static uword slp_crc(uword sofar, uword len, const ubyte *data);
				/* Calculate CRC */
static uword
palm_crc(uword sofar, uword len, const ubyte *data)
{
	extern uword Crc16CalcBlock(const ubyte *data, uword len, uword sofar);

	return Crc16CalcBlock(data, len, sofar);
}

/* magic_preamble
 * This is the preamble of every SLP packet. It never changes, so it
 * and its CRC are here to save time.
 */
/* XXX - ought to be 'const' */
static const ubyte magic_preamble[] = {
	0xbe, 0xef, 0xed,
};
static const ubyte preamble_checksum = 0x9a;
	/* Sum of the preamble elements, mod 0xff */
static const uword preamble_crc = 0xc9f1;	/* CRC of the preamble */

/* slp_crc
 * Calculate the CRC of a data packet. Shamelessly stolen from the
 * pilot-link package.
 */
static uword
slp_crc(uword sofar,	/* The CRC so far. This allows us to calculate
			 * a CRC of multiple disparate fragments. For
			 * new CRCs, this should be 0. */
	uword len,	/* Length of data */
	const ubyte *data)	/* The data itself */
{
	uword retval = sofar;

	for (; len > 0; len--)
	{
		int i;

		retval ^= (((uword) *data) << 8);
		data++;
		for (i = 0; i < 8; i++)
			if (retval & 0x8000)
			{
				retval <<= 1;
				retval ^= 0x1021;
			} else
				retval <<= 1;
	}
	return retval;
}

/* slp_recv
 * Receive a SLP packet for protocol 'protocol'.
 */
int
slp_recv(int fd, ubyte *buf, uword len,
	 struct slp_cb *control,
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
	err = read(fd, &c, 1);
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
	err = read(fd, &c, 1);
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
	err = read(fd, &c, 1);
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
		err = read(fd, &header_buf[got], want-got);
		if (err < 0)
		{
			perror("slp_read: read header");
			return -1;
		}
if (err == 0)
fprintf(stderr, "read() returned 0 at 4\n");
		got += err;
	}
	/* Dissect 'header_buf' and put the parts in '*header' */
	header->dst = header_buf[0];
	header->src = header_buf[1];
	header->type = header_buf[2];
	header->size = ((uword) header_buf[3] << 8) |
		header_buf[4];
	header->transID = header_buf[5];
	header->checksum = header_buf[6];

	/* Show the contents of the header */
	SLP_TRACE(1, "<   SLP: %d->%d, type %d, size %d, ID 0x%02x, sum 0x%02x\n",
		  header->src,
		  header->dst,
		  header->type,
		  header->size,
		  header->transID,
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
		err = read(fd, &body_buf[got], want-got);
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
		err = read(fd, &crc_buf[got], want-got);
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
	my_crc = palm_crc/*slp_crc*/(my_crc, sizeof(header_buf), header_buf);
	my_crc = palm_crc/*slp_crc*/(my_crc, header->size, body_buf);
	if (got_crc != my_crc)
	{
		fprintf(stderr, "CRC mismatch: expected 0x%04x, got 0x%04x\n",
			my_crc, got_crc);
		goto redo;
	}
SLP_TRACE(2, "Good CRC\n");
if (header->type == SLP_PACKET_TYPE_LOOPBACK)
goto redo;

	/* Now make sure this is the kind of packet the caller wanted */
	if (header->type != control->type)
	{
		SLP_TRACE(2, "SLP mismatch: wanted type %d, got %d\n",
			  control->type, header->type);
		goto redo;
	}

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
slp_send(int fd, ubyte *buf, uword len,
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
	memcpy(outbuf, magic_preamble, sizeof(magic_preamble));
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
		  header->transID,
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
	err = write(fd, outbuf, 9);
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
	err = write(fd, buf, len);
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

	err = write(fd, outbuf, 2);
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
	memcpy(pos, magic_preamble, sizeof(magic_preamble));
	pos += sizeof(magic_preamble);
	*pos++ = header->src;	/* These two are reversed */
	*pos++ = header->dst;
	*pos++ = header->type;
/*  	*pos++ = (len >> 8) & 0xff; */
/*  	*pos++ = len & 0xff; */
	*pos++ = (header->size >> 8) & 0xff;
	*pos++ = header->size & 0xff;
	*pos++ = header->transID;

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
	header.dst = outbuf[3];
	header.src = outbuf[4];
	header.type = outbuf[5];
	header.size = ((uword) outbuf[6] << 8) |
		outbuf[7];
	header.transID = outbuf[8];
	header.checksum = outbuf[9];

	/* Show the contents of the header */
	SLP_TRACE(1, ">   SLP: %d->%d, type %d, size %d, ID 0x%02x, sum 0x%02x\n",
		header.src,
		header.dst,
		header.type,
		header.size,
		header.transID,
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

	err = write(fd, outbuf, 10+len+2);
SLP_TRACE(2, "wrote %d/%d bytes\n", err, 10+len+2);
	if (err < 0)
	{
		perror("slp_send: write");
		return -1;
	}
	return err;
}
