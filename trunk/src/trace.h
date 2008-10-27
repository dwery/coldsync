/* trace.h
 *
 * Trace macros.
 *
 *	Copyright (C) 2002, Alessandro Zummo.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id$
 */

#define SYNC_TRACE(n)		if (sync_trace >= (n))
#define MISC_TRACE(n)		if (misc_trace >= (n))
#define PARSE_TRACE(n)		if (parse_trace >= (n))
#define CONDUIT_TRACE(n)	if (conduit_trace >= (n))
