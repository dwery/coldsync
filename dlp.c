#include <stdio.h>
#include <stdlib.h>	/* For malloc() */
#include "padp.h"
#include "dlp.h"

#ifdef DLP_TRACE
int dlp_debug = 0;
#endif	/* DLP_TRACE */

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
fprintf(stderr, "Finished sending request\n");

	/* Free 'outbuf' */
	free(outbuf);

	/* Wait for a response */
	dlp_read_resp(fd);

	return op;
}

int
dlp_read_resp(int fd
	      /* XXX */
	      )
{
	int i;
	int err;
	struct dlp_resp_header header;
	static ubyte inbuf[1024];

DLP_TRACE(2, "Awaiting DLP response\n");
	err = padp_recv(fd, (ubyte *) &header, 4);
DLP_TRACE(2, "DLP response: id 0x%02x (0x%02x), argc %d, errno %d\n",
	  header.id, (header.id & ~0x80), header.argc, header.errno);
return 0;
}
