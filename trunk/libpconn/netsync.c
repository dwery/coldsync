/* netsync.c
 *
 * NetSync-related functions.
 *
 * $Id: netsync.c,v 1.1 2000-12-10 21:40:54 arensb Exp $
 */

#include "config.h"
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

/*  #include "pconn/palm_types.h" */
/*  #include "pconn/palm_errno.h" */
/*  #include "pconn/slp.h" */
/*  #include "pconn/padp.h" */
/*  #include "pconn/util.h" */
#include "pconn/PConnection.h"
#include "pconn/netsync.h"

int net_trace = 10;		/* Debugging level for NetSync */
#define NET_TRACE(n)	if (net_trace >= (n))

extern int sockfd;		/* XXX - This is very bogus */
extern int data_sock;		/* XXX - This should be PConnection->fd */
static ubyte xid;		/* XXX - This goes in struct PConnection */
ubyte *inbuf;			/* XXX - Input buffer. This should be
				 * in PConnection.
				 */

/* netsync_init
 * Initialize the NetSync part of a PConnection.
 */
int
netsync_init(struct PConnection *pconn)
{
	/* Set the functions to send and receive DLP packets */
	pconn->dlp.read = netsync_read;
	pconn->dlp.write = netsync_write;

	return 0;
}

/* netsync_tini
 * Clean up the NetSync part of a PConnection that's being closed.
 */
int
netsync_tini(struct PConnection *pconn)
{
	/* XXX */
	return 0;
}

/* netsync_read
 * Read a NetSync packet from the given PConnection. A pointer to the
 * packet data (without the NetSync header) is put in '*buf'. The
 * length of the data (not counting the NetSync header) is put in
 * '*len'.
 *
 * If successful, returns a non-negative value. In case of error, returns a
 * negative value and sets 'palm_errno' to indicate the error.
 */
int
netsync_read(struct PConnection *pconn,	/* Connection to Palm */
	     const ubyte **buf,		/* Buffer to put the packet in */
	     uword *len)		/* Length of received message */
					/* XXX - Is a uword enough? */
{
	int err;
	ubyte hdr_buf[NETSYNC_HDR_LEN];	/* Unparsed header */
	struct netsync_header hdr;	/* Packet header */
	const ubyte *rptr;	/* Pointer into buffers, for reading */
	udword got;		/* How many bytes we've read so far */
	udword want;		/* How many bytes we still want to read */

	NET_TRACE(3)
		fprintf(stderr, "Inside netsync_read()\n");

	/* Read packet header */
	err = read(data_sock, hdr_buf, NETSYNC_HDR_LEN);
	if (err < 0)
	{
		fprintf(stderr, _("Error reading NetSync packet header.\n"));
		perror("read");
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

	/* XXX - Allocate space for the payload */
	if (inbuf == NULL)
	{
		inbuf = (ubyte *) malloc(hdr.len);
		/* XXX - Error-checking */
	} else {
		inbuf = (ubyte *) realloc(inbuf, hdr.len);
	}

	/* Read the payload */
	NET_TRACE(5)
		fprintf(stderr, "Reading packet data\n");
	want = hdr.len;
	got = 0;
	while (want > got)
	{
		err = read(data_sock, inbuf+got, want-got);
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
	}

	NET_TRACE(6)
		debug_dump(stderr, "NET <<<", inbuf, got);

	*buf = inbuf;		/* Tell caller where to find the data */
	*len = hdr.len;		/* And how much of it there was */

	return 1;		/* Success */
}

/* netsync_write
 * Write a NetSync message.
 */
int
netsync_write(struct PConnection *pconn,
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
	xid++;				/* Increment the XID for new request */
	wptr = out_hdr;
	put_ubyte(&wptr, 1);
	put_ubyte(&wptr, xid);
	put_udword(&wptr, len);

	/* Send the NetSync header */
	NET_TRACE(5)
	{
		fprintf(stderr, "Sending NetSync header (%d bytes)\n",
			len);
		debug_dump(stderr, "NET >>>", out_hdr, NETSYNC_HDR_LEN);
	}

	err = write(data_sock, out_hdr, NETSYNC_HDR_LEN);
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
		err = write(data_sock, buf+sent, want-sent);
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
