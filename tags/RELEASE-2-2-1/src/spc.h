/* spc.h
 *
 * Definitions, declarations pertaining to SPC (Serialized Procedure
 * Call).
 *
 *	Copyright (C) 1999, 2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: spc.h,v 2.5 2001-09-07 04:31:09 arensb Exp $
 *
 * Structure of an SPC request:
 *	[request header] [data]
 * The header contains an opcode and the length of the data. The form of
 * the data depends on the opcode.
 * XXX - Describe this in more detail.
 */
#ifndef _spc_h_
#define _spc_h_

#include "config.h"

/* SPC opcodes. The response opcodes have the same values as the request
 * opcodes, but with the high bit set.
 */
typedef enum {
	SPCOP_NOP = 0,		/* Do nothing. */
	SPCOP_DBINFO,		/* Request information about current
				 * database */
	SPCOP_DLPC,		/* DLP command */
	SPCOP_DLPR		/* RPC over DLP */
				/* XXX - Is this a good idea? Would it be
				 * better to force applications to send
				 * preformatted RPC-over-DLP packets?
				 */
} SPC_Op;

/* Status/error codes */
typedef enum {
	SPCERR_OK = 0,		/* No error */
	SPCERR_BADOP,		/* Unknown opcode */
	SPCERR_NOMEM		/* Out of memory */
} SPC_Errno;

/* spc_req_hdr
 * Header of an SPC request.
 */
struct spc_hdr {
	unsigned short op;	/* Opcode: what kind of request is this?
				 * This is one of the SPCOP_* constants,
				 * above. */
	unsigned short status;	/* Return status. Ignored when sending
				 * request. In the response, this is one of
				 * the SPCERR_* constants, above */
	unsigned long len;	/* Length of data following this header */
};

#define SPC_HEADER_LEN	8	/* Length of SPC header */

extern int spc_send(struct spc_hdr *header,
		    PConnection *pconn,
		    const struct dlp_dbinfo *dbinfo,
		    const unsigned char *inbuf,
		    unsigned char **outbuf);

#endif	/* _spc_h_ */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
