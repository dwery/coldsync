/* dlp.c
 *
 * Implementation of the Desktop Link Protocol (DLP).
 *
 * Note that the functions in this file just implement the protocol
 * itself. They are not really expected to be called by conduits or
 * other user programs: for them, see the DLP convenience functions in
 * dlp_cmd.c.
 *
 * $Id: dlp.c,v 1.2 1999-01-23 23:08:11 arensb Exp $
 */
#include <stdio.h>
#include <stdlib.h>	/* For malloc() */
#include "padp.h"
#include "dlp.h"

#ifdef DLP_TRACE
int dlp_debug = 0;
#endif	/* DLP_TRACE */

/* dlp_errlist
 * List of DLP error messages. The DLP error code can be used as an
 * index into this array.
 */
char *dlp_errlist[] = {
	"No error",
	"General Palm system error",
	"Illegal request ID",
	"Insufficient memory",
	"Invalid parameter",
	"Database, record or resource not found",
	"No open databases",
	"Database is open by someone else",
	"Too many open databases",
	"Database already exists",
	"Can't open database",
	"Record is deleted",
	"Record is in use by someone else",
	"Operation not supported on this database type",
	"*UNUSED*",
	"You do not have write access",
	"Not enough space for record",
	"Size limit exceeded",
	"Cancel the sync",
	"Bad argument wrapper",		/* For debugging */
	"Required argument not found",	/* For debugging */
	"Invalid argument size",
};

/* XXX - Is this a good idea? */
void
dlp_free_arglist(int argc, struct dlp_arg *argv)
{
	int i;

	if (argv == NULL)	/* Trivial case */
		return;

	for (i = 0; i < argc; i++)
	{
		if (argv[i].data == NULL)
			continue;

		free(argv[i].data);
	}
	free(argv);
}

/* dlp_send_req
 * Send a DLP request to the Palm.
 */
/* XXX - Perhaps write a varargs version of this function? */
int
dlp_send_req(int fd,		/* File descriptor on which to send */
	     struct dlp_req_header *header,
				/* Request header */
	     struct dlp_arg argv[])
				/* The arguments themselves */
{
	int i;
	int err;
	ubyte *outbuf;		/* Output buffer */
	int op;			/* Index (pointer) into 'outbuf' */
	udword len;		/* Total length of request */

	/* Start by figuring out how long the request is going to be */

	len = 2;		/* Length of request header */

	/* For each argument, figure out whether it's tiny, small or
	 * long[1], and how much data it has, and hence how much space
	 * it's going to take up.
	 *
	 * [1] aka wee, not sae wee, and freakin' huuge!
	 */
	for (i = 0; i < header->argc; i++)
	{
		DLP_TRACE(3, "dlp_send_req: len == %d\n", len);
		DLP_TRACE(3, "dlp_send_req: argv[%d].size == %d\n",
			  i, argv[i].size);
		if (argv[i].size <= DLP_TINY_ARG_MAX)
		{
			DLP_TRACE(3, "dlp_send_req: argv[%d] tiny\n", i);
			/* Use a tiny arg for this argument */
			len += 2;	/* Size of tiny arg header */
		} else if (argv[i].size <= DLP_SMALL_ARG_MAX)
		{
			DLP_TRACE(3, "dlp_send_req: argv[%d] small\n", i);
			/* Use a small arg for this argument */
			len += 4;	/* Size of small arg header */
		} else {
			DLP_TRACE(3, "dlp_send_req: argv[%d] long\n", i);
			/* Use a long arg for this argument */
			len += 6;	/* Size of long arg header */
		}

		len += argv[i].size;	/* Size of the argument's data */
	}
	DLP_TRACE(2, "dlp_send_req: len == %d\n", len);

	/* Allocate space for 'outbuf'. This is where we'll write the
	 * request itself.
	 */
	if ((outbuf = (ubyte *) malloc(len)) == NULL)
	{
		fprintf(stderr, "dlp_send_req: Can't malloc outbuf\n");
		return -1;
	}

	/* Now proceed to write the request into 'outbuf' */
	/* Start with the header */
	op = 0;				/* Next byte to be written */
	outbuf[op++] = header->id;
	outbuf[op++] = header->argc;

	/* Now add each argument in turn */
	for (i = 0; i < header->argc; i++)
	{
		/* XXX - Should also test the argv[i].id that was
		 * given, in case the caller wants to, say, manually
		 * send a tiny amount of data inside a long argument,
		 * just for yucks.
		 */
		if (argv[i].size <= DLP_TINY_ARG_MAX)
		{
			/* Use a tiny header */
			outbuf[op++] = argv[i].id & ~0x80;
					/* Argument ID. Make sure the
					 * high bit is clear, since
					 * it's a tiny arg. */
			outbuf[op++] = argv[i].size;
						
		} else if (argv[i].size <= DLP_SMALL_ARG_MAX)
		{
			/* Use a small header */
			outbuf[op++] = argv[i].id | 0x80;
					/* Argument ID. Make sure the
					 * high bit is set, since this
					 * is a small arg.
					 */
			outbuf[op++] = 0;	/* Unused, for alignment */
			outbuf[op++] = (argv[i].size >> 8) & 0xff;
			outbuf[op++] = argv[i].size & 0xff;
		} else {
			/* Use a long header */
			outbuf[op++] = ((argv[i].id >> 8) | 0xc0) & 0xff;
					/* High byte of argument ID.
					 * Make sure the high two bits
					 * are set, since this is a
					 * long arg.
					 */
			outbuf[op++] = argv[i].id & 0xff;
					/* Low byte of argument ID. */
			/* The 4-byte argument size */
			outbuf[op++] = (argv[i].size >> 24) & 0xff;
			outbuf[op++] = (argv[i].size >> 16) & 0xff;
			outbuf[op++] = (argv[i].size >> 8) & 0xff;
			outbuf[op++] = argv[i].size & 0xff;
		}

		/* Append the argument data itself */
		memcpy(outbuf+op, argv[i].data, argv[i].size);
		op += argv[i].size;
	}

	/* Send the argument */
	err = padp_send(fd, outbuf, op);
	if (err < 0)
	{
		fprintf(stderr, "dlp_send_req: error in padp_send()\n");
		free(outbuf);
		return err;
	}
	DLP_TRACE(2, "Finished sending request\n");

	/* Free 'outbuf' */
	free(outbuf);

	return op;
}

/* dlp_read_resp
 *
 * Read a response to a DLP request.
 */
int
dlp_read_resp(int fd,		/* File descriptor to read from */
	      struct dlp_resp_header *header,
				/* Response header */
	      struct dlp_arg **argv_ret)
				/* Response arguments will go here */
{
	int i;
	int err;
	static ubyte inbuf[1024];	/* XXX - How big should this
					 * be? Perhaps allocate based
					 * on buf? */
	ubyte *iptr;		/* Pointer into 'inbuf', for parsing */

	DLP_TRACE(2, "Awaiting DLP response\n");

	/* Read a PADP packet */
	if ((err = padp_recv(fd, inbuf, sizeof(inbuf))) < 0)
	{
		fprintf(stderr, "dlp_read_resp: padp_recv failed: %d\n",
			err);
		return err;
	}

	/* Copy the header information to 'header' */
	header->id = inbuf[0];	/* XXX - Check to make sure the high bit is set: it's a response */
	header->argc = inbuf[1];
	header->errno = ((dword) inbuf[2] << 8) |
		inbuf[3];
	DLP_TRACE(2, "DLP response: id 0x%02x (0x%02x), argc %d, errno %d\n",
		  header->id, (header->id & ~0x80), header->argc,
		  header->errno);

	/* Allocate memory for the argument array */
	if ((*argv_ret = (struct dlp_arg *) calloc(header->argc,
						    sizeof(struct dlp_arg)))
	    == NULL)
	{
		fprintf(stderr, "Can't allocate %d dlp_arg's\n", header->argc);
		return -1;
	}

	/* Parse the rest of the message into arguments */
	DLP_TRACE(3, "Parsing response arguments\n");
	iptr = inbuf + 4;		/* Point to first argument */
	for (i = 0; i < header->argc; i++)
	{
		/* Figure out the size of the argument by looking at
		 * the ID. */
		switch (*iptr & 0xc0)
		{
		    case 0x00:
		    case 0x40:
			/* It's a tiny argument */
			DLP_TRACE(3, "arg %d is tiny\n", i);
			(*argv_ret)[i].id = *iptr;
			iptr++;
			(*argv_ret)[i].size = (udword) *iptr++;
			break;
		    case 0x80:
			/* It's a small argument */
			DLP_TRACE(3, "arg %d is small\n", i);
			(*argv_ret)[i].id = *iptr;
			iptr++;
			iptr++;		/* Skip unused byte */
			(*argv_ret)[i].size = ((udword) iptr[0] << 8) |
				iptr[1];
			iptr += 2;
			break;
		    case 0xc0:
			/* It's a long argument */
			DLP_TRACE(3, "arg %d is long\n", i);
			(*argv_ret)[i].id = ((udword) iptr[0] << 8) |
				iptr[1];
			iptr += 2;
			(*argv_ret)[i].size =
				((udword) iptr[0] << 24) |
				((udword) iptr[1] << 16) |
				((udword) iptr[2] << 8) |
				iptr[3];
			iptr += 4;
			break;
		    default:
			/* This should never happen */
			fprintf(stderr, "Arithmetic has failed me. The world is coming to an end!\n");
			break;
		}
		DLP_TRACE(2, "DLP response arg %d: id 0x%x, size %d\n",
			  i, (*argv_ret)[i].id, (*argv_ret)[i].size);

		/* Got the argument structure. Now allocate and copy
		 * the data.
		 */
		if (((*argv_ret)[i].data = malloc((*argv_ret)[i].size))
		    == NULL)
		{
			fprintf(stderr, "Can't allocate memory for arg %d\n", i);
			/* XXX - Free argv_ret itself */
			return -1;
		}
		memcpy((*argv_ret)[i].data, iptr, (*argv_ret)[i].size);
		iptr += (*argv_ret)[i].size;
	}
return 0;
}
