/* spc.c
 *
 * Functions for handling SPC (Serialized Procedure Call) protocol.
 *
 *	Copyright (C) 1999, 2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: spc.c,v 2.1 2000-07-31 09:01:08 arensb Exp $
 */

#include "config.h"
#include <stdio.h>
#include "pconn/pconn.h"	/* For DLP and debug_dump() */
#include "coldsync.h"
#include "spc.h"

/* spc_send
 * Takes an SPC header, the data associated with it, and a pointer to
 * someplace to return the results.
 * spc_send() parses the header and does the Right Thing with it,
 * depending on the opcode in the header. In the case of DLP requests,
 * this means passing the data along to the Palm and reading a
 * response.
 * spc_send() modifies 'header', filling in the return status for the
 * request, and changing its 'len' field to the length of the return
 * data.
 * NB: the data returned in '*outbuf', if any, is allocated by
 * spc_send(); it is the caller's responsibility to free it
 * afterwards.
 */
int
spc_send(struct spc_hdr *header,
	 const unsigned char *inbuf,
	 unsigned char **outbuf)
{
	SYNC_TRACE(5)
	{
		fprintf(stderr, "* Inside spc_send(%d, (%d), %ld))\n",
			header->op, header->status, header->len);
		debug_dump(stderr, "SPC", inbuf, header->len);
	}

	/* Decide what to do based on the opcode */
	switch (header->op)
	{
	    case SPCOP_NOP:		/* Do nothing */
		header->status = SPCERR_OK;
		header->len = 0L;
		*outbuf = NULL;		/* No return data */
		return 0;

	    default:			/* Bad opcode */
		header->status = SPCERR_BADOP;
		header->len = 0L;
		*outbuf = NULL;		/* No return data */
		return 0;	/* Return success, because spc_send()
				 * successfully processed a malformed
				 * request.
				 */
	}

	return 0;		/* Success */
}
