/* PConnection.h
 *
 * Defines the PConnection abstraction, which embodies a connection
 * to a P device.
 *
 * $Id: PConnection.h,v 1.1 1999-01-24 04:56:48 arensb Exp $
 */
#ifndef _PConn_h_
#define _PConn_h_

#include "SLP_private.h"
#include "PADP_private.h"
#include "CMP_private.h"
#include "DLP_private.h"

/* PConnection
 * This struct is an opaque type that contains all of the state about
 * a connection to a given Palm device at a given time. There is a
 * common part and several per-protocol parts, to encourage
 * encapsulation.
 * Programs should never use the PConnection type directly.
 * Instead, everything should use the PConnHandle type, defined
 * below.
 */
struct PConnection
{
	/* Common part */
	/* XXX - Nothing yet */
/*  	struct PConnection *next; */

	/* Protocol-dependent parts */
	SLPPart slp;
	PADPPart padp;
	CMPPart cmp;
	DLPPart dlp;
};

/* PConnHandle
 * This is an opaque type used to refer to a particular connection.
 * Programs should not try to peek inside (just like X11's Widgets),
 * nor should they assume that this is a pointer.
 * XXX - At some point, it would be nice if this could become an
 * honest to God socket type, at which point you'd just have
 *	typedef int PConnHandle;
 */
typedef struct PConnection *PConnHandle;

extern PConnHandle new_PConnection(/*XXX*/);

#endif	/* _PConn_h_ */
