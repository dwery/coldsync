/* spc.h
 *
 * Definitions, declarations pertaining to SPC (Serialized Procedure
 * Call).
 *
 *	Copyright (C) 1999, 2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: spc.h,v 2.1 2000-07-31 09:00:47 arensb Exp $
 *
 * Structure of an SPC request:
 *	[request header] [data]
 * The header contains an opcode and the length of the data. The form of
 * the depends entirely on the opcode.
 */
#ifndef _spc_h_
#define _spc_h_

#include "config.h"

/* spc_req_hdr
 * Header of an SPC request.
 */
struct spc_hdr {
	unsigned short op;	/* Opcode: what kind of request is this?
				 * This is one of the SPCOP_* constants,
				 * below. */
	unsigned short status;	/* Return status. Ignored when sending
				 * request. In the response, this is one of
				 * the SPCERR_* constants, below */
	unsigned long len;	/* Length of data following this header */
};

#define SPC_HEADER_LEN	8	/* Length of SPC header */

/* SPC opcodes. The response opcodes have the same values as the request
 * opcodes, but with the high bit set.
 */
/* XXX - Would it be better to return the status in the opcode slot? After
 * all, the client knows what response it sent.
 */
#define SPCOP_NOP	0	/* Do nothing. */
#define SPCOP_DBINFO	1	/* Request information about current
				 * database */
#define SPCOP_DLPC	2	/* DLP command */
#define SPCOP_DLPR	3	/* RPC over DLP */
				/* XXX - Is this a good idea? Would it be
				 * better to force applications to send
				 * preformatted RPC-over-DLP packets?
				 */

/* Status/error codes */
#define SPCERR_OK	0	/* No error */
#define SPCERR_BADOP	1	/* Unknown opcode */

/* XXX - Need a function to actually do this stuff. Presumably, it should
 * be:
 *	int handle_spc(
 *		const struct spc_req_hdr *header,
 *		const ubyte *inbuf,
 *		ubyte **reply,
 *		long *reply_len);
 * The header is in '*header', 'inbuf' is a buffer containing the request
 * data. handle_spc() runs, allocates memory for the response, and stuffs
 * the reply in '*reply' (and the length of the reply in '*reply_len').
 *
 * Should handle_spc() also put the response header in '*reply'? That would
 * probably be a bad idea: there will always be a reply header, but perhaps
 * there will not always be any other response data. So that would save a
 * couple of extra malloc()s.
 */

extern int spc_send(struct spc_hdr *header,
		    const unsigned char *inbuf,
		    unsigned char **outbuf);

#endif	/* _spc_h_ */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
