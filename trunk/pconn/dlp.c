/* dlp.c
 *
 * Implementation of the Desktop Link Protocol (DLP).
 *
 * Note that the functions in this file just implement the protocol
 * itself. They are not really expected to be called by conduits or
 * other user programs: for them, see the DLP convenience functions in
 * dlp_cmd.c.
 *
 * $Id: dlp.c,v 1.4 1999-02-24 13:12:04 arensb Exp $
 */
#include <stdio.h>
#include <stdlib.h>		/* For calloc() */
#include <string.h>		/* For memcpy() et al. */
#include "palm/palm_types.h"
#include "palm_errno.h"
#include "dlp.h"
#include "padp.h"
#include "util.h"
#include "PConnection.h"

#define DLP_DEFAULT_ARGV_LEN	10	/* Initial length of argv, in
					 * PConnection. */

#define DLP_DEBUG	1
#ifdef DLP_DEBUG

int dlp_debug = 0;

#define DLP_TRACE(level, format...)		\
	if (dlp_debug >= (level))		\
		fprintf(stderr, "DLP:" format)

#endif	/* DLP_DEBUG */

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
	static ubyte outbuf[2048];	/* Outgoing request buffer */
					/* XXX - Fixed size: bad */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	palm_errno = PALMERR_NOERR;

	/* XXX - Ought to figure out the total length of the request
	 * and allocate a buffer for it.
	 */

	/* Construct a DLP request header in the output buffer */
	wptr = outbuf;
	put_ubyte(&wptr, header->id);
	put_ubyte(&wptr, header->argc);
	DLP_TRACE(5, ">>> request id 0x%02x, %d args\n",
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
			DLP_TRACE(10, "Tiny argument %d, id 0x%02x, size %ld\n",
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
			DLP_TRACE(10, "Small argument %d, id 0x%02x, size %ld\n",
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
			DLP_TRACE(10, "Long argument %d, id 0x%04x, size %ld\n",
				  i, argv[i].id, argv[i].size);
			put_uword(&wptr, (argv[i].id & 0x3fff) | 0xc000);
					/* Make sure the high two bits are
					 * 11, since this is a long
					 * argument.
					 */
			put_udword(&wptr, argv[i].size);
		}

		/* Append the argument data to the header */
		/* XXX - Potential buffer overrun */
		memcpy(wptr, argv[i].data, argv[i].size);
		wptr += argv[i].size;
	}

	/* Send the request */
	err = padp_write(pconn, outbuf, wptr-outbuf);
	if (err < 0)
	{
		/* XXX - (After the outgoing buffer is dynamically
		 * allocated): free the outgoing buffer.
		 */
		return err;
	}

	/* XXX - (After the outgoing buffer is dynamically
	 * allocated): free the outgoing buffer.
	 */
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
	header->errno = get_uword(&rptr);
	DLP_TRACE(6, "Got response, id 0x%02x, argc %d, errno %d\n",
		  header->id,
		  header->argc,
		  header->errno);

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
			DLP_TRACE(5, "Arg %d is long\n", i);
			pconn->dlp.argv[i].id = get_uword(&rptr);
			pconn->dlp.argv[i].size = get_udword(&rptr);
			break;
		    case 0x80:		/* Small argument */
			DLP_TRACE(5, "Arg %d is small\n", i);
			pconn->dlp.argv[i].id = get_ubyte(&rptr);
			get_ubyte(&rptr);	/* Skip over padding */
			pconn->dlp.argv[i].size = get_uword(&rptr);
			break;
		    default:		/* Tiny argument */
			DLP_TRACE(5, "Arg %d is tiny\n", i);
			pconn->dlp.argv[i].id = get_ubyte(&rptr);
			pconn->dlp.argv[i].size = get_ubyte(&rptr);
			break;
		}
		pconn->dlp.argv[i].id &= 0x3f;	/* Stip off the size bits */
		DLP_TRACE(6, "Got arg %d, id 0x%02x, size %ld\n",
			  i, pconn->dlp.argv[i].id, pconn->dlp.argv[i].size);
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
