/* palm_errno.h
 *
 * Error codes for libpalm.
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: palm_errno.h,v 1.5 2001-09-08 01:12:06 arensb Exp $
 */
#ifndef _palm_errno_h_
#define _palm_errno_h_

/* Error codes */
typedef enum {
	PALMERR_NOERR		= 0,	/* No error */
	PALMERR_SYSTEM,			/* Error in system call.
					 * Consult `errno' */
	PALMERR_NOMEM,			/* Out of memory */
	PALMERR_TIMEOUT,		/* A timeout occurred */
	PALMERR_BADF,			/* Bad file descriptor */
	PALMERR_EOF,			/* End of file */
	PALMERR_ABORT,			/* Palm has aborted */
	PALMERR_BADID,			/* Invalid request ID */
	PALMERR_BADRESID,		/* Invalid result ID */
	PALMERR_BADARGID,		/* Invalid argument ID */
	PALMERR_ACKXID			/* XID on ACK doesn't match request */
} palmerr_t;

extern palmerr_t palm_errno;		/* Error code */

extern const char *palm_strerror(const palmerr_t palm_errno);

#endif	/* _palm_errno_h_ */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
