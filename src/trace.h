/* trace.h
 *
 * Trace macros.
 *
 *	Copyright (C) 2002, Alessandro Zummo.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: trace.h,v 1.1 2002-04-17 16:51:14 azummo Exp $
 */

#define SYNC_TRACE(n)	if (sync_trace >= (n))
#define MISC_TRACE(n)	if (misc_trace >= (n))
#define PARSE_TRACE(n)	if (parse_trace >= (n))
