/* pconn.h
 * Simple include file that includes all of the pconn-related .h files
 *
 *	Copyright (C) 1999-2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: pconn.h,v 1.5.8.1 2001-07-29 07:26:08 arensb Exp $
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

#define NETSYNC_WAKEUP_MAGIC    0xfade
#define NETSYNC_WAKEUP_PORT     14237   /* UDP port on which the client
                                         * sends out the wakeup request.
                                         */
#define NETSYNC_DATA_PORT       14238   /* TCP port on which the client and
                                         * server exchange sync data.
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

#endif	/* __pconn_h__ */
