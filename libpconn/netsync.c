/* netsync.c
 *
 * NetSync-related functions.
 *
 * $Id: netsync.c,v 1.9 2001-07-30 07:19:48 arensb Exp $
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

/* XXX - Could these be CMP 2.0? When answering this question, might want
 * to keep in mind the underlying protocol, the one with the (other) XIDs,
 * implemented by netsync_read() and netsync_write().
 */
ubyte ritual_resp1[] = {
	0x90,				/* Command */
	0x01,				/* argc */
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x20,		/* Arg ID */
	0x00, 0x00, 0x00, 0x08,		/* Arg length */
	/* Arg data */
	0x00, 0x00, 0x00, 0x01,
	0x80, 0x00, 0x00, 0x00,
};

ubyte ritual_stmt2[] = {
	0x12,				/* Command */
	0x01,				/* argc */
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x20,		/* Arg ID */
	0x00, 0x00, 0x00, 0x24,		/* Arg length */
	/* Arg data */
	0xff, 0xff, 0xff, 0xff,
	0x3c, 0x00,			/* These are reversed in the
					 * response */
	0x3c, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0xc0, 0xa8, 0xa5, 0x1f,		/* 192.168.165.31 */
	0x04, 0x27, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
};

ubyte ritual_resp2[] = {
	0x92,				/* Command */
	0x01,				/* argc */
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x20,		/* Arg ID */
	0x00, 0x00, 0x00, 0x24,		/* Arg length */
	/* Arg data */
	0xff, 0xff, 0xff, 0xff,
	0x00, 0x3c,
	0x00, 0x3c,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x01,
	0xc0, 0xa8, 0x84, 0x3c,		/* 192.168.132.60
					 * Presumably, this is the IP
					 * address (or hostid) of the
					 * sender.
					 */
	0x04, 0x1c, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
};

ubyte ritual_stmt3[] = {
	0x13,				/* Command */
	0x01,				/* argc */
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x20,		/* Arg ID */
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

ubyte ritual_resp3[] = {
	0x93,				/* Command */
	0x00,				/* argc? */
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00,
};

/* ritual_exch_server
 * Exchange ritual packets with the Palm. We are acting as the server
 * (i.e., the other end originated the connection).
 * It is not known what the ritual packets are. This function doesn't try
 * to analyze them or anything.
 * Returns 0 if successful, -1 in case of error.
 */
int
ritual_exch_server(PConnection *pconn)
{
	int err;
	const ubyte *inbuf;
	uword inlen;

	/* Receive ritual response 1 */
	/* Agh! This is hideous! Apparently NetSync and m50x modes are
	 * identical except for just this one packet!
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
	err = netsync_write(pconn, ritual_stmt2, ritual_stmt2_size);
	IO_TRACE(5)
		fprintf(stderr, "netsync_write(ritual stmt 2) returned %d\n",
			err);
	if (err < 0)
		/* XXX - Indicate error */
		return -1;

	/* Receive ritual response 2 */
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
	err = netsync_write(pconn, ritual_stmt3, ritual_stmt3_size);
	IO_TRACE(5)
		fprintf(stderr, "netsync_write(ritual stmt 3) returned %d\n",
			err);
	if (err < 0)
		/* XXX - Indicate error */
		return -1;

	/* Receive ritual response 3 */
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
	ubyte inbuf[1024];		/* XXX - Fixed size: bad */
	const ubyte *netbuf;		/* Buffer from netsync layer */
	uword inlen;

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
	err = netsync_read(pconn, &netbuf, &inlen);
	/* XXX - Error-checking */
	IO_TRACE(5)
	{
		fprintf(stderr,
			"netsync_read(ritual stmt 2) returned %d\n",
			err);
		if (err > 0)
			debug_dump(stderr, "<<<", inbuf, inlen);
	}
	if (err < 0)
		/* XXX - Indicate error */
		return -1;

	/* Send ritual response 2 */
	err = netsync_write(pconn, ritual_resp1, ritual_resp2_size);
	/* XXX - Error-checking */
	IO_TRACE(5)
		fprintf(stderr, "netsync_write(ritual resp 2) returned %d\n",
			err);
	if (err < 0)
		/* XXX - Indicate error */
		return -1;

	/* Receive ritual statement 3 */
	err = netsync_read(pconn, &netbuf, &inlen);
	/* XXX - Error-checking */
	IO_TRACE(5)
	{
		fprintf(stderr,
			"netsync_read(ritual stmt 3) returned %d\n",
			err);
		if (err > 0)
			debug_dump(stderr, "<<<", inbuf, inlen);
	}
	if (err < 0)
		/* XXX - Indicate error */
		return -1;

	/* Send ritual response 3 */
	err = netsync_write(pconn, ritual_resp1, ritual_resp3_size);
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

	NET_TRACE(3)
		fprintf(stderr, "Inside netsync_read()\n");

	if (!no_header)
	{
		/* Read packet header */
	  	err = (*pconn->io_read)(pconn, hdr_buf, NETSYNC_HDR_LEN);
		if (err < 0)
		{
		  fprintf(stderr, _("Error reading NetSync packet header.\n"));
		  perror("read");
				/* XXX - Does (*pconn->io_read) set errno? */
		  return -1;	/* XXX - Ought to retry */
		} else if (err != NETSYNC_HDR_LEN)
		{
		  fprintf(stderr,
			  _("Error: only read %d bytes of NetSync packet "
			    "header.\n"),
			  err);
		  return -1;	/* XXX - Ought to continue reading */
		}

		/* Parse the header */
		rptr = hdr_buf;
		hdr.cmd = get_ubyte(&rptr);
		hdr.xid = get_ubyte(&rptr);
		hdr.len = get_udword(&rptr);

		NET_TRACE(5)
		  fprintf(stderr,
			  "Got header: cmd 0x%02x, xid 0x%02x, len 0x%08lx\n",
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
		fprintf(stderr, "Reading packet data\n");
	want = hdr.len;
	got = 0;
	while (want > got)
	{
		err = (*pconn->io_read)(pconn, pconn->net.inbuf+got, want-got);
		if (err < 0)
		{
			perror("netsync_read: read");
			palm_errno = PALMERR_SYSTEM;
			return -1;
		}
		if (err == 0)
		{
			NET_TRACE(5)
				fprintf(stderr, "EOF in packet.\n");
			palm_errno = PALMERR_EOF;
			return 0;
		}
		got += err;
		NET_TRACE(6)
			fprintf(stderr, "want: %ld, got: %ld\n", want, got);
	}

	NET_TRACE(6)
		debug_dump(stderr, "NET <<<", pconn->net.inbuf, got);

	*buf = pconn->net.inbuf;	/* Tell caller where to find the
					 * data */
	*len = hdr.len;			/* And how much of it there was */

	return 1;			/* Success */
}

/* netsync_write
 * Write a NetSync message.
 */
int
netsync_write(PConnection *pconn,
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
	bump_xid(pconn);		/* Increment the XID for new request */
	wptr = out_hdr;
	put_ubyte(&wptr, 1);
	put_ubyte(&wptr, pconn->net.xid);
	put_udword(&wptr, len);

	/* Send the NetSync header */
	NET_TRACE(5)
	{
		fprintf(stderr, "Sending NetSync header (%d bytes)\n",
			len);
		debug_dump(stderr, "NET >>>", out_hdr, NETSYNC_HDR_LEN);
	}

	err = (*pconn->io_write)(pconn, out_hdr, NETSYNC_HDR_LEN);
	NET_TRACE(7)
		fprintf(stderr, "write() returned %d\n", err);
	if (err < 0)
	{
		fprintf(stderr, _("Error sending NetSync header.\n"));
		return -1;
	}

	/* Send the packet data */
	want = len;
	sent = 0;
	while (sent < want)
	{
		err = (*pconn->io_write)(pconn, buf+sent, want-sent);
		if (err < 0)
		{
			perror("netsync_write: write");
			palm_errno = PALMERR_SYSTEM;
			return -1;
		}
		sent += err;
	}

	NET_TRACE(6)
		debug_dump(stderr, "NET >>>", buf, len);

	return len;		/* Success */
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
