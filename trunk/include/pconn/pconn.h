/* pconn.h
 * Simple include file that includes all of the pconn-related .h files
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: pconn.h,v 1.7 2001-07-09 10:36:17 arensb Exp $
 */
#ifndef __pconn_h__
#define __pconn_h__

#define CARD0	0		/* Memory card #0. The only real purpose of
				 * this is to make it marginally easier to
				 * find all of the places where card #0 has
				 * been hardcoded, once support for
				 * multiple memory cards is added, if it
				 * ever is.
				 */

/* Debugging variables */
extern int slp_trace;		/* Debugging level for Serial Link Protocol */
extern int cmp_trace;		/* Debugging level for Connection
				 * Management Protocol */
extern int padp_trace;		/* Debugging level for Packet
				 * Assembly/Disassembly Protocol. */
extern int dlp_trace;		/* Debugging level for Desktop Link
				 * Protocol */
extern int dlpc_trace;		/* Debugging level for Desktop Link
				 * Protocol commands */

#include <pconn/dlp_cmd.h>

/* XXX - Instead of including a bunch of files here, should just put
 * all of the libpconn prototypes here.
 */
#include <pconn/palm_errno.h>
#include <pconn/cmp.h>
#include <pconn/dlp_cmd.h>
#include <pconn/dlp_rpc.h>
#include <pconn/netsync.h>
#include <pconn/util.h>

extern uword crc16(const ubyte *buf, uword len, const uword start);

#endif	/* __pconn_h__ */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
