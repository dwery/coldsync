/* coldsync.h
 *
 * Error codes and whatnot for the ColdSync core.
 *
 *	Copyright (C) 1999, 2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: cs_error.h,v 2.7 2002-09-03 19:21:22 azummo Exp $
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
	CSE_NOCONN		/* Lost connection to Palm */,
	CSE_PALMERR,		/* Protocol error */
	CSE_DLPERR,		/* DLP error */
} CSErrno;

extern CSErrno cs_errno;		/* ColdSync error status */

extern void update_cs_errno_dlp(PConnection *pconn);
extern void update_cs_errno_pconn(PConnection *pconn, palmerrno_t palm_errno);

extern void print_cs_errno(CSErrno cs_errno);
extern void print_latest_dlp_error(PConnection *pconn);

#define cs_errno_fatal(x) ( x != CSE_NOERR )

#endif	/* _cs_error_h_ */

/* This is for Emacs's benefit:
 * Local Variables:	***
 * fill-column:	75	***
 * End:			***
 */
