/* netsync.c
 *
 * NetSync-related functions.
 *
 * $Id: netsync.c,v 1.21 2002-05-03 17:31:13 azummo Exp $
 */

#include "config.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

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

#include "pconn/PConnection.h"
#include "pconn/netsync.h"

int net_trace = 0;		/* Debugging level for NetSync */
#define NET_TRACE(n)	if (net_trace >= (n))

/*
 * Ritual statements
 * These packets are sent back and forth during the initial handshaking
 * phase. I don't know what they mean. The sequence is:
 * client sends UDP wakeup packet
 * server sends UDP wakeup ACK
 * client sends ritual response 1
 * server sends ritual statement 2
 * client sends ritual response 2
 * server sends ritual statement 3
 * client sends ritual response 3
 *
 * The comments are mostly conjecture and speculation.
 */
/* The ritual packets appear to be similar to DLP, but with more
 * udwords:
 *
 * struct ritual_packet
 * {
 *	ubyte cmd;		// cmd | 0x80, for responses
 *	ubyte argc;
 *	udword errno;
 *	ritual_arg argv[];	// 'argc' number of arguments
 * };
 *
 * struct ritual_argv
 * {
 *	udword len;
 *	ubyte data[];		// 'len' bytes of data
 * };
 */
/* XXX - http://hcirisc.cs.binghamton.edu/pipermail/pilot-unix/2001-July/004238.html
 * also mentions the following packet:
 *
 * +SLP HDR [ 0xbe 0xef 0xed 0x03 0x03 0x02 0x00 0x41 0x08 0xeb]
 * +SLP RX 3->3 len=0x0041 Prot=2 ID=0x08
 * +PADP DATA RX FL  len=0x003d
 * +0000  90 01 00 00 20 37 00 00 30 39 00 00 00 00 00 00   .... 7..09......
 * +0010  69 1d 07 d1 05 16 00 2a 1a 00 07 d1 05 16 00 2a   i......*.......*
 * +0020  1a 00 09 10 4a 6f 65 20 55 73 65 72 00 20 2c b9   ....Joe User. ,.
 * +0030  62 ac 59 07 5b 96 4b 07 15 2d 23 4b 70            b.Y.[.K..-#Kp
 *
 * The last 16 bytes are the md5 hash of the password ("123" in this case).
 */

static ubyte ritual_resp1[] = {
	0x90,				/* Command */
	0x01,				/* argc */
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x20,		/* Arg ID */
	0x00, 0x00, 0x00, 0x08,		/* Arg length */
	/* Arg data */
	0x01, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00,
};


static const int ritual_resp1_size =
	sizeof(ritual_resp1) / sizeof(ritual_resp1[0]);

/* First packet sent by NetSync daemon, when forwarding a NetSync
 * connection:
        01 ff 00 00 00 16
        90 01 00 00 00 00 00 00 00 20 00 00 00 08 00 00
        00 02 00 00 00 00
*/

static ubyte ritual_stmt2[] = {
	0x12,				/* Command */
	0x01,				/* argc */
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00,

	0x00, 0x20,			/* Arg ID */
	0x00, 0x00, 0x00, 0x24,		/* Arg length */

	/* Arg data */
	0xff, 0xff, 0xff, 0xff,
	0x3c, 0x00,			/* These are reversed in the
					 * response */
	0x3c, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0xc0, 0xa8, 0x01, 0x21,		/* 192.168.165.31 */
	0x04, 0x27, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
};
static const int ritual_stmt2_size =
	sizeof(ritual_stmt2) / sizeof(ritual_stmt2[0]);

static ubyte ritual_resp2[] = {
	0x92,				/* Command */
	0x01,				/* argc */
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x20,		/* Arg ID */
	0x00, 0x00, 0x00, 0x24,		/* Arg length */
	/* Arg data */
	0xff, 0xff, 0xff, 0xff,
	0x00, 0x3c,
	0x00, 0x3c,
	0x40, 0x00, 0x00, 0x00,
	0x01, 0x00, 0x00, 0x00,
	0xc0, 0xa8, 0xa5, 0x1e,		/* 192.168.132.60
					 * Presumably, this is the IP
					 * address (or hostid) of the
					 * sender.
					 */
	0x04, 0x01, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
};
static const int ritual_resp2_size =
	sizeof(ritual_resp2) / sizeof(ritual_resp2[0]);

static ubyte ritual_stmt3[] = {
	0x13,				/* Command */
	0x01,				/* argc */
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00,

	0x00, 0x20,			/* Arg ID */
	0x00, 0x00, 0x00, 0x20,		/* Arg length */

	/* Arg data
	 * This is very similar to ritual statement/response 2.
	 */
	0xff, 0xff, 0xff, 0xff,
	0x00, 0x3c,
	0x00, 0x3c,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x01,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 
};
static const int ritual_stmt3_size =
	sizeof(ritual_stmt3) / sizeof(ritual_stmt3[0]);

/* resp3
NET <<< 93 00 00 00 00 00 00 00
	91 00 00 00 00 00 00 00
*/

static ubyte ritual_resp3[] = {
	0x93,				/* Command */
	0x00,				/* argc? */
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00,
};
static const int ritual_resp3_size =
	sizeof(ritual_resp3) / sizeof(ritual_resp3[0]);

/* ritual_exch_server
 * Exchange ritual packets with the Palm. We are acting as the server
 * (i.e., the other end originated the connection).
 * It is not known what the ritual packets are. This function doesn't try
 * to analyze them or anything.
 * Returns 0 if successful, -1 in case of error.
 */
/* XXX - This can hang under Linux. One workaround is to select() before
 * reading the response statements, to make sure the Palm received the
 * original request. Give it 2 seconds or so. If select() times out, 'goto
 * retry2;' (or whichever) and try resending the request.
 */
int
ritual_exch_server(PConnection *pconn)
{
	int err;
	const ubyte *inbuf;
	uword inlen;

	/* Receive ritual response 1 */
	IO_TRACE(6)
		fprintf(stderr, "ritual_exch_server: receiving "
			"ritual response 1\n");
	/* Agh! This is hideous! Apparently NetSync and m50x modes are
	 * identical except for just this one packet!
	 */
	/* XXX - Actually, no: this appears to be due to a bug in Linux's
	 * USB-to-serial thingy.
	 */
	switch (pconn->protocol)
	{
	    case PCONN_STACK_NET:
		err = netsync_read_method(pconn, &inbuf, &inlen, False);
		break;

	    case PCONN_STACK_SIMPLE:
		inlen = ritual_resp1_size;
		err = netsync_read_method(pconn, &inbuf, &inlen, True);
		break;

	    case PCONN_STACK_NONE:
	    case PCONN_STACK_DEFAULT:
	    case PCONN_STACK_FULL:
		/* XXX - Error */
	    default:
		/* XXX - No other protocols should be using this. Notify of
		 * error: unsupported protocol.
		 */
		return -1;
	}

	IO_TRACE(5)
	{
		fprintf(stderr,
			"netsync_read(ritual resp 1) returned %d\n",
			err);
		if (err > 0)
			debug_dump(stderr, "<<<", inbuf, inlen);
	}
	if (err < 0)
		/* XXX - Indicate error */
		return -1;

	/* Send ritual statement 2 */
	IO_TRACE(6)
		fprintf(stderr, "sending ritual statement 2\n");

	err = netsync_write(pconn, ritual_stmt2, ritual_stmt2_size);
	IO_TRACE(5)
		fprintf(stderr, "netsync_write(ritual stmt 2) returned %d\n",
			err);
	if (err < 0)
		/* XXX - Indicate error */
		return -1;

	/* Receive ritual response 2 */
	IO_TRACE(6)
		fprintf(stderr, "receiving ritual response 2\n");

	err = netsync_read(pconn, &inbuf, &inlen);
	IO_TRACE(5)
	{
		fprintf(stderr, "netsync_read returned %d\n", err);
		if (err > 0)
			debug_dump(stderr, "<<<", inbuf, inlen);
	}
	if (err < 0)
		/* XXX - Indicate error */
		return -1;

	/* Send ritual statement 3 */
	IO_TRACE(6)
		fprintf(stderr, "sending ritual statement 3\n");


	err = netsync_write(pconn, ritual_stmt3, ritual_stmt3_size);
	IO_TRACE(5)
		fprintf(stderr, "netsync_write(ritual stmt 3) returned %d\n",
			err);
	if (err < 0)
		/* XXX - Indicate error */
		return -1;

	/* Receive ritual response 3 */
	IO_TRACE(6)
		fprintf(stderr, "receiving ritual response 3\n");

	err = netsync_read(pconn, &inbuf, &inlen);
	IO_TRACE(5)
	{
		fprintf(stderr, "netsync_read returned %d\n", err);
		if (err > 0)
			debug_dump(stderr, "<<<", inbuf, inlen);
	}
	if (err < 0)
		/* XXX - Indicate error */
		return -1;

	return 0;
}

/* ritual_exch_client
 * Exchange ritual packets with the Palm. We are acting as the client
 * (i.e., we originated the connection).
 * The ritual packets are quite mysterious. This function doesn't try to
 * analyze them or anything.
 * Returns 0 if successful, -1 in case of error.
 */
int
ritual_exch_client(PConnection *pconn)
{
	int err;
	const ubyte *netbuf;		/* Buffer from netsync layer */
	uword inlen;

	IO_TRACE(6)
		fprintf(stderr, "ritual_exch_client: sending "
			"ritual response 1\n");

	/* Send ritual response 1 */
	err = netsync_write(pconn, ritual_resp1, ritual_resp1_size);
	/* XXX - Error-checking */
	IO_TRACE(5)
		fprintf(stderr, "netsync_write(ritual resp 1) returned %d\n",
			err);
	if (err < 0)
		/* XXX - Indicate error */
		return -1;

	/* Receive ritual statement 2 */
	IO_TRACE(6)
		fprintf(stderr, "ritual_exch_client: receiving "
			"ritual statement 2\n");
	err = netsync_read(pconn, &netbuf, &inlen);
	/* XXX - Error-checking */
	IO_TRACE(5)
	{
		fprintf(stderr,
			"netsync_read(ritual stmt 2) returned %d, len %d\n",
			err, inlen);
		if (err > 0)
			debug_dump(stderr, "<<<", netbuf, inlen);
	}
	if (err < 0)
		/* XXX - Indicate error */
		return -1;

	/* Send ritual response 2 */
	IO_TRACE(6)
		fprintf(stderr, "ritual_exch_client: sending "
			"ritual response 2\n");
	err = netsync_write(pconn, ritual_resp2, ritual_resp2_size);
	/* XXX - Error-checking */
	IO_TRACE(5)
		fprintf(stderr, "netsync_write(ritual resp 2) returned %d\n",
			err);
	if (err < 0)
		/* XXX - Indicate error */
		return -1;

	/* Receive ritual statement 3 */
	IO_TRACE(6)
		fprintf(stderr, "ritual_exch_client: receiving "
			"ritual statement 3\n");
	err = netsync_read(pconn, &netbuf, &inlen);
	/* XXX - Error-checking */
	IO_TRACE(5)
	{
		fprintf(stderr,
			"netsync_read(ritual stmt 3) returned %d, len %d\n",
			err, inlen);
		if (err > 0)
			debug_dump(stderr, "<<<", netbuf, inlen);
	}
	if (err < 0)
		/* XXX - Indicate error */
		return -1;

	/* Send ritual response 3 */
	IO_TRACE(6)
		fprintf(stderr, "ritual_exch_client: sending "
			"ritual response 3\n");

	err = netsync_write(pconn, ritual_resp3, ritual_resp3_size);
	/* XXX - Error-checking */
	IO_TRACE(5)
		fprintf(stderr, "netsync_write(ritual resp 3) returned %d\n",
			err);
	if (err < 0)
		/* XXX - Indicate error */
		return -1;

	return 0;
}

/* bump_xid
 * Pick a new NetSync transaction ID by incrementing the existing one.
 * XXX - If, in fact, NetSync uses PADP, then there might be reserved XIDs,
 * in which case this function will have to be updated to cope (see
 * libpconn/padp.c for an example).
 */
static void
bump_xid(PConnection *pconn)
{
	pconn->net.xid++;		/* Increment the current xid */

	if( pconn->net.xid == 0x00 )
		pconn->net.xid = 0x01;
}

/* netsync_init
 * Initialize the NetSync part of a PConnection.
 */
int
netsync_init(PConnection *pconn)
{
	/* Set the functions to send and receive DLP packets */
	pconn->dlp.read = netsync_read;
	pconn->dlp.write = netsync_write;
	pconn->net.inbuf = NULL;

	return 0;
}

/* netsync_tini
 * Clean up the NetSync part of a PConnection that's being closed.
 */
int
netsync_tini(PConnection *pconn)
{
	if (pconn == NULL)
		return 0;

	if (pconn->net.inbuf != NULL)
		free(pconn->net.inbuf);
	return 0;
}

int
netsync_read(PConnection *pconn,	/* Connection to Palm */
	     const ubyte **buf,		/* Buffer to put the packet in */
	     uword *len)		/* Length of received message */
					/* XXX - Is a uword enough? */

{
	return netsync_read_method(pconn, buf, len, False);
}

/* netsync_read
 * Read a NetSync packet from the given PConnection. A pointer to the
 * packet data (without the NetSync header) is put in '*buf'. The
 * length of the data (not counting the NetSync header) is put in
 * '*len'.
 * If 'no_header' is true, then netsync_read() does not attempt to read the
 * packet header. Instead, it reads the expected length of the packet from
 * *len.
 *
 * If successful, returns a non-negative value. In case of error, returns a
 * negative value and sets 'palm_errno' to indicate the error.
 */
int
netsync_read_method(PConnection *pconn,	/* Connection to Palm */
		    const ubyte **buf,	/* Buffer to put the packet in */
		    uword *len,		/* Length of received message */
		    			/* XXX - Is a uword enough? */
		    const Bool no_header)
					/* m50x starts without a header! */
{
	int err;
	ubyte hdr_buf[NETSYNC_HDR_LEN];	/* Unparsed header */
	struct netsync_header hdr;	/* Packet header */
	const ubyte *rptr;	/* Pointer into buffers, for reading */
	udword got;		/* How many bytes we've read so far */
	udword want;		/* How many bytes we still want to read */
	struct timeval timeout;	/* How long to wait for incoming data */

	NET_TRACE(3)
		fprintf(stderr, "Inside netsync_read()\n");

	if (!no_header)
	{
		/* Read packet header */
		NET_TRACE(5)
			fprintf(stderr,
				"netsync_read: Reading packet header\n");

		/* Wait for there to be something to read */
		timeout.tv_sec = NETSYNC_WAIT_TIMEOUT;
		timeout.tv_usec = 0L;
		err = PConn_select(pconn, forReading, &timeout);
		if (err == 0)
		{
			/* select() timed out */
			PConn_set_palmerrno(pconn, PALMERR_TIMEOUT2);
			return -1;
		}

		/* Now we can read the packet */
	  	err = PConn_read(pconn, hdr_buf, NETSYNC_HDR_LEN);
		if (err < 0)
		{
			fprintf(stderr,
				_("Error reading NetSync packet header.\n"));
			perror("read");
				/* XXX - Does PConn_read set errno? */
			return -1;
		} else if (err != NETSYNC_HDR_LEN)
		{
			fprintf(stderr,
				_("Error: only read %d bytes of NetSync "
				  "packet header.\n"),
				err);
			PConn_set_palmerrno(pconn, PALMERR_SYSTEM);
			return -1;
		}

		NET_TRACE(7)
		{
			fprintf(stderr, "netsync_read: read %d bytes:\n", err);
			debug_dump(stderr, "NET <<<", hdr_buf, err);
		}

		/* Parse the header */
		rptr = hdr_buf;
		hdr.cmd = get_ubyte(&rptr);
		hdr.xid = get_ubyte(&rptr);
		hdr.len = get_udword(&rptr);

		/* If we have initiated the connection, we must use the
		 * server provided XID in our packets
		 */

		if (pconn->whosonfirst)
			pconn->net.xid = hdr.xid;
		

		NET_TRACE(5)
			fprintf(stderr,
				"Got header: cmd 0x%02x, xid 0x%02x, "
				"len 0x%08lx\n",
				hdr.cmd, hdr.xid, hdr.len);

		/* XXX - What to do if cmd != 1? */
	} else
		hdr.len = *len;

	/* Allocate space for the payload */
	if (pconn->net.inbuf == NULL)
	{
		pconn->net.inbuf = (ubyte *) malloc(hdr.len);
		/* XXX - Error-checking */
	} else {
		pconn->net.inbuf = (ubyte *)
		realloc(pconn->net.inbuf, hdr.len);
		/* XXX - Error-checking */
	}

	/* Read the payload */
	NET_TRACE(5)
		fprintf(stderr, "netsync_read: Reading packet data\n");
	want = hdr.len;
	got = 0;
	while (want > got)
	{
		/* Wait for there to be something to read */
		timeout.tv_sec = NETSYNC_WAIT_TIMEOUT;
		timeout.tv_usec = 0L;
		err = PConn_select(pconn, forReading, &timeout);
		if (err == 0)
		{
			/* select() timed out */
			PConn_set_palmerrno(pconn, PALMERR_TIMEOUT2);
			return -1;
		}

		/* Now we can read the packet */
		err = PConn_read(pconn, pconn->net.inbuf+got, want-got);
		if (err < 0)
		{
			perror("netsync_read_method: read");
			return -1;
		}
		else if (err == 0)
		{
			NET_TRACE(5)
				fprintf(stderr, "EOF in packet.\n");
			return 0;
		}

		NET_TRACE(8)
			debug_dump(stderr, "NET <<<", pconn->net.inbuf+got, err);
		got += err;
		NET_TRACE(7)
			fprintf(stderr, "netsync_read: want: %ld, got: %ld\n", want, got);
	}

	NET_TRACE(6)
		debug_dump(stderr, "NET <<<", pconn->net.inbuf, got);

	*buf = pconn->net.inbuf;	/* Tell caller where to find the
					 * data */
	*len = hdr.len;			/* And how much of it there was */

	return 1;			/* Success */
}

/* netsync_write_old - Old version.
 * Write a NetSync message.
 */
int
netsync_write_old(PConnection *pconn,
	      const ubyte *buf,
	      const uword len)		/* XXX - Is this enough? */
{
	int err;
	ubyte out_hdr[NETSYNC_HDR_LEN];	/* Buffer for outgoing header */
	ubyte *wptr;			/* Pointer into buffer, for writing */
	udword sent;			/* How many bytes we've sent */
	udword want;			/* How many bytes we want to send */

	NET_TRACE(3)
		fprintf(stderr, "Inside netsync_write()\n");

	/* Construct the NetSync header */

	if (pconn->whosonfirst == 0)
		bump_xid(pconn);	/* Get the XID for new request */


	wptr = out_hdr;
	put_ubyte(&wptr, 1);
	put_ubyte(&wptr, pconn->net.xid);
	put_udword(&wptr, len);

	/* Send the NetSync header */
	NET_TRACE(5)
	{
		fprintf(stderr, "Sending NetSync header (%d bytes)\n",
			NETSYNC_HDR_LEN);
		debug_dump(stderr, "NET >>>", out_hdr, NETSYNC_HDR_LEN);
	}

	err = PConn_write(pconn, out_hdr, NETSYNC_HDR_LEN);
	NET_TRACE(7)
		fprintf(stderr, "write() returned %d\n", err);
	if (err < 0)
	{
		fprintf(stderr, _("Error sending NetSync header.\n"));
		return -1;
	}

	/* Send the packet data */
	NET_TRACE(5)
	{
		fprintf(stderr, "Sending NetSync data (%d bytes)\n", len);
		debug_dump(stderr, "NET >>>", buf, len);
	}
	want = len;
	sent = 0;
	while (sent < want)
	{
		err = PConn_write(pconn, buf+sent, want-sent);
		if (err < 0)
		{
			perror("netsync_write: write");
			return -1;
		}
		sent += err;
	}

	return len;		/* Success */
}

/* netsync_write - New version. 
 * Write a NetSync message. The same packet will have header and data.
 * Very useful when debugging and comparing packets...
 */

int
netsync_write(PConnection *pconn,
	      const ubyte *buf,
	      const uword len)		/* XXX - Is this enough? */
{
	int err;
	ubyte *wptr;			/* Pointer into buffer, for writing */
	ubyte *outbuf;
	udword sent;			/* How many bytes we've sent */
	udword want;			/* How many bytes we want to send */

	NET_TRACE(3)
		fprintf(stderr, "Inside netsync_write()\n");

	outbuf = malloc(len + 6);

	if(outbuf == NULL)
		return -1; /* XXX - Maybe -ENOMEM would be better? */

	/* Construct the NetSync header */

	if (pconn->whosonfirst == 0)
		bump_xid(pconn);	/* Get the XID for new request */

	wptr = outbuf;
	put_ubyte(&wptr, 1);
	put_ubyte(&wptr, pconn->net.xid);
	put_udword(&wptr, len);

	/* Copy the payload */
	memcpy( outbuf + NETSYNC_HDR_LEN, buf, len);


	/* Send the NetSync header */
	NET_TRACE(5)
	{
		fprintf(stderr, "Sending NetSync header (6 bytes)\n");
		debug_dump(stderr, "NET >>>", outbuf, NETSYNC_HDR_LEN);
	}

	/* Send the packet data */
	NET_TRACE(5)
	{
		fprintf(stderr, "Sending NetSync data (%d bytes)\n", len);
		debug_dump(stderr, "NET >>>", buf, len);
	}


	want = len+6;
	sent = 0;
	while (sent < want)
	{
		err = PConn_write(pconn, outbuf+sent, want-sent);
		if (err < 0)
		{
			perror("netsync_write: write");
			free(outbuf);
			return -1;
		}
		sent += err;
	}

	free(outbuf);

	return len;		/* Success */
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
