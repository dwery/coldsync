/* coldsync.h
 *
 * Error codes and whatnot for the ColdSync core.
 *
 *	Copyright (C) 1999, 2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: cs_error.h,v 2.4 2001-09-07 03:25:16 arensb Exp $
 */
#ifndef _cs_error_h_
#define _cs_error_h_

typedef enum CSErrno {
	CSE_NOERR = 0,		/* No error */
	CSE_OTHER,		/* None of the above (below?) */
	CSE_CANCEL,		/* Sync cancelled by user */
				/* XXX - Is it worth subdividing this
				 * further into "cancelled by Palm" and
				 * "killed by Ctrl-C on the desktop"?
				 */
	CSE_NOCONN		/* Lost connection to Palm */
} CSErrno;

extern CSErrno cs_errno;		/* ColdSync error status */

#endif	/* _cs_error_h_ */

/* This is for Emacs's benefit:
 * Local Variables:	***
 * fill-column:	75	***
 * End:			***
 */
