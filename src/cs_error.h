/* coldsync.h
 *
 * Error codes and whatnot for the ColdSync core.
 *
 *	Copyright (C) 1999, 2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: cs_error.h,v 2.1 2000-05-06 11:43:14 arensb Exp $
 */
#ifndef _cs_error_h_
#define _cs_error_h_

extern int cs_errno;		/* ColdSync error status */

#define CSE_NOERR	0		/* No error */
#define CSE_CANCEL	1		/* Sync cancelled by user */
					/* XXX - Is it worth subdividing
					 * this further into "cancelled by
					 * Palm" and "killed by Ctrl-C on
					 * the desktop"?
					 */

#endif	/* _cs_error_h_ */

/* This is for Emacs's benefit:
 * Local Variables:	***
 * fill-column:	75	***
 * End:			***
 */
