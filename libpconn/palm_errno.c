/* palm_errno.c
 *
 * Error-related stuff for libpalm.
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: palm_errno.c,v 1.2 1999-11-27 05:46:27 arensb Exp $
 */
#include "config.h"
#include <pconn/palm_errno.h>

int palm_errno;				/* Current error code */

/* XXX - Need something to keep this array and the error codes in sync */
/* Error messages corresponding to the PALMERR_* constants in
 * "palm_errno.h". Make sure these stay in sync.
 */
const char *palm_errlist[] = {		/* Error messages */
/* PALMERR_NOERR */	"No error",
/* PALMERR_SYSTEM */	"Error in system call or library function",
/* PALMERR_NOMEM */	"Out of memory",
/* PALMERR_TIMEOUT */	"Timeout",
/* PALMERR_BADF */	"Bad file descriptor",
/* PALMERR_EOF */	"End of file",
/* PALMERR_ABORT */	"Transfer aborted",
/* PALMERR_BADID */	"Invalid request ID",
/* PALMERR_BADRESID */	"Invalid result ID",
/* PALMERR_BADARGID*/	"Invalid argument ID",
};
/* XXX - Need something to check the size of this array, so if someone adds
 * an error code and forgets to update this array, you can keep from
 * reading off the end of the array.
 */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
