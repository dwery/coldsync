/* PalmConn.h
 *
 * Defines the PalmConnection abstraction, which embodies a connection
 * to a Palm device.
 *
 * $Id: PalmConn.h,v 1.1 1999-01-24 04:32:47 arensb Exp $
 */
#ifndef _PalmConn_h_
#define _PalmConn_h_

#include "SLP_private.h"
#include "PADP_private.h"
#include "CMP_private.h"
#include "DLP_private.h"

/* PalmConnection
 * This struct is an opaque type that contains all of the state about
 * a connection to a given Palm at a given time. There is a common
 * part and several per-protocol parts, to encourage encapsulation.
 * Programs should never use the PalmConnection type directly.
 * Instead, everything should use the PalmConnHandle type, defined
 * below.
 */
struct PalmConnection
{
	/* Common part */
	/* XXX - Nothing yet */

	/* Protocol-dependent parts */
	SLPPart slp;
	PADPPart padp;
	CMPPart cmp;
	DLPPart dlp;
};

/* PalmConnHandle
 * This is an opaque type used to refer to a particular connection.
 * Programs should not try to peek inside (just like X11's Widgets),
 * nor should they assume that this is a pointer.
 * XXX - At some point, it would be nice if this could become an
 * honest to God socket type, at which point you'd just have
 *	typedef int PalmConnHandle;
 */
typedef struct PalmConnection *PalmConnHandle;

#endif	/* _PalmConn_h_ */
