/* palm_errno.c
 *
 * Error-related stuff for libpalm.
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: palm_errno.c,v 1.5 2001-03-16 14:08:31 arensb Exp $
 */
#include "config.h"

/* Include I18N-related stuff, if necessary */
#if HAVE_LIBINTL_H
#  include <locale.h>		/* For setlocale() and friends */
#  include <libintl.h>
#endif	/* HAVE_LIBINTL_H */

#include <pconn/palm_errno.h>

int palm_errno;				/* Current error code */

/* XXX - Need something to keep this array and the error codes in sync */
/* Error messages corresponding to the PALMERR_* constants in
 * "palm_errno.h". Make sure these stay in sync.
 */
const char *palm_errlist[] = {		/* Error messages */
/* PALMERR_NOERR */	N_("No error"),
/* PALMERR_SYSTEM */	N_("Error in system call or library function"),
/* PALMERR_NOMEM */	N_("Out of memory"),
/* PALMERR_TIMEOUT */	N_("Timeout"),
/* PALMERR_BADF */	N_("Bad file descriptor"),
/* PALMERR_EOF */	N_("End of file"),
/* PALMERR_ABORT */	N_("Transfer aborted"),
/* PALMERR_BADID */	N_("Invalid request ID"),
/* PALMERR_BADRESID */	N_("Invalid result ID"),
/* PALMERR_BADARGID*/	N_("Invalid argument ID"),
};

const int palm_numerrs = sizeof(palm_errlist)/sizeof(palm_errlist[0]);

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
