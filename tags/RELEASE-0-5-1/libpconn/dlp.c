/* dlp.c
 *
 * Implementation of the Desktop Link Protocol (DLP).
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * Note that the functions in this file just implement the protocol
 * itself. They are not really expected to be called by conduits or
 * other user programs: for them, see the DLP convenience functions in
 * dlp_cmd.c.
 *
 * $Id: dlp.c,v 1.2 1999-11-04 10:44:58 arensb Exp $
 */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>		/* For calloc() */

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

#include <pconn/palm_types.h>
#include <pconn/palm_errno.h>
#include <pconn/dlp.h>
#include <pconn/padp.h>
#include <pconn/util.h>
#include <pconn/PConnection.h>

int dlp_trace = 0;			/* Debugging level for DLP */

#define DLP_TRACE(n)	if (dlp_trace >= (n))

#define DLP_DEFAULT_ARGV_LEN	10	/* Initial length of argv, in
					 * PConnection. */

/* dlp_init
 * Initialize the DLP part of a new PConnection.
 */
int
dlp_init(struct PConnection *pconn)
{
	/* Allocate a new argv[] of some default size */
	if ((pconn->dlp.argv =
	    (struct dlp_arg *) calloc(sizeof(struct dlp_arg),
				      DLP_DEFAULT_ARGV_LEN))
		== NULL)
	{
		return -1;
	}
	pconn->dlp.argv_len = DLP_DEFAULT_ARGV_LEN;

	return 0;
}

int 
dlp_tini(struct PConnection *pconn)
{
	/* Free the argv */
	if (pconn->dlp.argv != NULL)
		free(pconn->dlp.argv);

	return 0;
}

/* dlp_send_req
 * Send the DLP request defined by 'header'. 'argv' is the list of
 * arguments.
 * Returns 0 if successful. In case of error, returns a negative
 * value. 'palm_errno' is set to indicate the error.
 */
int
dlp_send_req(struct PConnection *pconn,		/* Connection to Palm */
	     struct dlp_req_header *header,
	     				/* Request header */
	     struct dlp_arg argv[])	/* Array of request arguments */
{
	int i;
	int err;
	ubyte *outbuf;			/* Outgoing request buffer */
	long buflen;			/* Length of outgoing request */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	palm_errno = PALMERR_NOERR;

	/* Calculate size of outgoing request */
	DLP_TRACE(6)
		fprintf(stderr,
			"dlp_send_req: Calculating outgoing request buffer\n");

	buflen = 2L;		/* Request id and argc */
	for (i = 0; i < header->argc; i++)
	{
		if (argv[i].size <= DLP_TINYARG_MAXLEN)
		{
			/* Tiny argument */
			buflen += 2 + argv[i].size;
					/* 2 bytes for id and 1-byte size */
			DLP_TRACE(7)
				fprintf(stderr, "Tiny argument: %ld bytes, "
					"buflen == %ld\n",
					argv[i].size, buflen);
		} else if (argv[i].size <= DLP_SMALLARG_MAXLEN)
		{
			/* Small argument */
			buflen += 4 + argv[i].size;
					/* 4 bytes for id, unused, and
					 * 2-byte size */
			DLP_TRACE(7)
				fprintf(stderr, "Small argument: %ld bytes, "
					"buflen == %ld\n",
					argv[i].size, buflen);
		} else {
			/* Long argument */
			buflen += 6 + argv[i].size;
					/* 6 bytes: 2-byte id and 4-byte
					 * size */
			DLP_TRACE(7)
				fprintf(stderr, "Long argument: %ld bytes, "
					"buflen == %ld\n",
					argv[i].size, buflen);
		}
	}

	/* Allocate a buffer of the proper length */
	outbuf = (ubyte *) malloc(buflen);
	if (outbuf == NULL)
	{
		fprintf(stderr,
			"dlp_send_req: Can't allocate %ld-byte buffer\n",
			buflen);
		return -1;
	}

	/* Construct a DLP request header in the output buffer */
	wptr = outbuf;
	put_ubyte(&wptr, header->id);
	put_ubyte(&wptr, header->argc);
	DLP_TRACE(5)
		fprintf(stderr, ">>> request id 0x%02x, %d args\n",
			header->id, header->argc);

	/* Append the request headers to the output buffer */
	for (i = 0; i < header->argc; i++)
	{
		/* See whether this argument ought to be tiny, small
		 * or large, and construct an appropriate header.
		 */
		if (argv[i].size <= DLP_TINYARG_MAXLEN)
		{
			/* Tiny argument */
			DLP_TRACE(10)
				fprintf(stderr,
					"Tiny argument %d, id 0x%02x, "
					"size %ld\n",
					i, argv[i].id, argv[i].size);
			put_ubyte(&wptr, argv[i].id & 0x3f);
					/* Make sure the high two bits are
					 * 00, since this is a tiny
					 * argument.
					 */
			put_ubyte(&wptr, argv[i].size);
		} else if (argv[i].size <= DLP_SMALLARG_MAXLEN)
		{
			/* Small argument */
			DLP_TRACE(10)
				fprintf(stderr,
					"Small argument %d, id 0x%02x, "
					"size %ld\n",
					i, argv[i].id, argv[i].size);
			put_ubyte(&wptr, (argv[i].id & 0x3f) | 0x80);
					/* Make sure the high two bits are
					 * 10, since this is a small
					 * argument.
					 */
			put_ubyte(&wptr, 0);		/* Padding */
			put_uword(&wptr, argv[i].size);
		} else {
			/* Long argument */
			/* XXX - Check to make sure the comm. protocol
			 * supports long arguments.
			 */
			DLP_TRACE(10)
				fprintf(stderr,
					"Long argument %d, id 0x%04x, size %ld\n",
					i, argv[i].id, argv[i].size);
			put_uword(&wptr, (argv[i].id & 0x3fff) | 0xc000);
					/* Make sure the high two bits are
					 * 11, since this is a long
					 * argument.
					 */
			put_udword(&wptr, argv[i].size);
		}

		/* Append the argument data to the header */
		memcpy(wptr, argv[i].data, argv[i].size);
		wptr += argv[i].size;
	}

	/* Send the request */
	err = padp_write(pconn, outbuf, wptr-outbuf);
	if (err < 0)
	{
		free(outbuf);
		return err;
	}

	free(outbuf);
	return 0;		/* Success */
}

/* dlp_recv_resp
 * Receive the response to a DLP request. The response header will be
 * put in 'header', and the arguments will be written to 'argv'. No
 * more than 'argc' arguments will be written.
 * Returns 0 if successful. In case of error, returns a negative
 * value; 'palm_errno' is set to indicate the error.
 */
int
dlp_recv_resp(struct PConnection *pconn,	/* Connection to Palm */
	      const ubyte id,	/* ID of the original request */
	      struct dlp_resp_header *header,
				/* Response header will be put here */
	      const struct dlp_arg **argv)
				/* Where to put the arguments */
{
	int i;
	int err;
	const ubyte *inbuf;	/* Input data (from PADP) */
	uword inlen;		/* Length of input data */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */

	/* Read the response */
	err = padp_read(pconn, &inbuf, &inlen);
	if (err < 0)
		return err;	/* Error */

	/* Parse the response header */
	rptr = inbuf;
	header->id = get_ubyte(&rptr);
	header->argc = get_ubyte(&rptr);
	header->error = get_uword(&rptr);
	DLP_TRACE(6)
		fprintf(stderr, "Got response, id 0x%02x, argc %d, errno %d\n",
			header->id,
			header->argc,
			header->error);

	/* Make sure it's really a DLP response */
	if ((header->id & 0x80) != 0x80)
	{
		fprintf(stderr, "##### Expected a DLP response, but this isn't one!\n");
		return -1;
	}

	/* Make sure the response ID matches the request ID */
	if ((header->id & 0x7f) != id)
	{
		fprintf(stderr, "##### Bad response ID: expected 0x%02x, got 0x%02x\n",
			id | 0x80, header->id);
		palm_errno = PALMERR_BADID;
		return -1;
	}

	/* Make sure there's room for all of the arguments */
	if (header->argc > pconn->dlp.argv_len)
	{
		struct dlp_arg *eptr;	/* Pointer to reallocated argv */

		/* Grow argv. Use the temporary variable 'eptr' in case
		 * realloc() fails.
		 */
		eptr = (struct dlp_arg *)
			realloc(pconn->dlp.argv,
				sizeof(struct dlp_arg) * header->argc);
		if (eptr == NULL)
		{
			/* Reallocation failed */
			/* XXX - Set an error code */
			return -1;
		}
		/* Update the new argv */
		pconn->dlp.argv = eptr;
		pconn->dlp.argv_len = header->argc;
	}

	/* Parse the arguments */
	for (i = 0; i < header->argc; i++)
	{
		/* See if it's a tiny, small or long argument */
		switch (*rptr & 0xc0)
		{
		    case 0xc0:		/* Long argument */
			DLP_TRACE(5)
				fprintf(stderr, "Arg %d is long\n", i);
			pconn->dlp.argv[i].id = get_uword(&rptr);
			pconn->dlp.argv[i].size = get_udword(&rptr);
			break;
		    case 0x80:		/* Small argument */
			DLP_TRACE(5)
				fprintf(stderr, "Arg %d is small\n", i);
			pconn->dlp.argv[i].id = get_ubyte(&rptr);
			get_ubyte(&rptr);	/* Skip over padding */
			pconn->dlp.argv[i].size = get_uword(&rptr);
			break;
		    default:		/* Tiny argument */
			DLP_TRACE(5)
				fprintf(stderr, "Arg %d is tiny\n", i);
			pconn->dlp.argv[i].id = get_ubyte(&rptr);
			pconn->dlp.argv[i].size = get_ubyte(&rptr);
			break;
		}
		pconn->dlp.argv[i].id &= 0x3f;	/* Stip off the size bits */
		DLP_TRACE(6)
			fprintf(stderr, "Got arg %d, id 0x%02x, size %ld\n",
				i, pconn->dlp.argv[i].id,
				pconn->dlp.argv[i].size);
		pconn->dlp.argv[i].data = (ubyte *) rptr;
		rptr += pconn->dlp.argv[i].size;
	}

	*argv = pconn->dlp.argv;
	return 0;
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
