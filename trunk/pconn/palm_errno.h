/* palm_errno.h
 *
 * Error codes for libpalm.
 *
 * $Id: palm_errno.h,v 1.2 1999-02-21 08:14:23 arensb Exp $
 */
#ifndef _palm_errno_h_
#define _palm_errno_h_

/* Error codes */
#define PALMERR_NOERR		0	/* No error */
#define PALMERR_SYSTEM		1	/* Error in system call.
					 * Consult `errno' */
#define PALMERR_NOMEM		2	/* Out of memory */
#define PALMERR_TIMEOUT		3	/* A timeout occurred */
#define PALMERR_BADF		4	/* Bad file descriptor */
#define PALMERR_EOF		5	/* End of file */
#define PALMERR_ABORT		6	/* Palm has aborted */
#define PALMERR_BADID		7	/* Invalid request ID */
#define PALMERR_BADRESID	8	/* Invalid result ID */
#define PALMERR_BADARGID	9	/* Invalid argument ID */

extern int palm_errno;			/* Error code */
extern const char *palm_errlist[];	/* List of error messages */

/* XXX - palm_perror(), perhaps? */

#endif	/* _palm_errno_h_ */
