/* coldsync.h
 *
 * Error codes and whatnot for the ColdSync core.
 *
 *	Copyright (C) 1999, 2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: cs_error.h,v 2.3 2001-06-26 05:51:50 arensb Exp $
 */
#ifndef _cs_error_h_
#define _cs_error_h_

extern int cs_errno;		/* ColdSync error status */

#define CSE_NOERR	0		/* No error */
#define CSE_OTHER	1		/* None of the above (below?) */
#define CSE_CANCEL	2		/* Sync cancelled by user */
					/* XXX - Is it worth subdividing
					 * this further into "cancelled by
					 * Palm" and "killed by Ctrl-C on
					 * the desktop"?
					 */
#define CSE_NOCONN	3		/* Lost connection to Palm */

#endif	/* _cs_error_h_ */

/* This is for Emacs's benefit:
 * Local Variables:	***
 * fill-column:	75	***
 * End:			***
 */
