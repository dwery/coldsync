/* palm_errno.c
 *
 * Error-related stuff for libpalm.
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: palm_errno.c,v 1.7 2001-12-09 22:44:21 arensb Exp $
 */
#include "config.h"

/* Include I18N-related stuff, if necessary */
#if HAVE_LIBINTL_H
#  include <locale.h>		/* For setlocale() and friends */
#  include <libintl.h>
#endif	/* HAVE_LIBINTL_H */

#include <pconn/palm_errno.h>

palmerr_t palm_errno;				/* Current error code */

/* palm_strerror
 * Given an error code, return the error message corresponding to that
 * error code.
 */
/* XXX - I18N: eventually, presumably this should do the translation
 * itself, instead of forcing the calling function to do so.
 */
const char *
palm_strerror(const palmerr_t errno)
{
	/* This is implemented as a switch statement and not as an array
	 * lookup in order to allow the compiler to make sure that all
	 * error codes have an error message.
	 */
	switch (errno)
	{
	    case PALMERR_NOERR:
		return N_("No error");
	    case PALMERR_SYSTEM:
		return N_("Error in system call or library function");
	    case PALMERR_NOMEM:
		return N_("Out of memory");
	    case PALMERR_TIMEOUT:
		return N_("Timeout");
	    case PALMERR_TIMEOUT2:
		return N_("Unexpected timeout");
	    case PALMERR_BADF:
		return N_("Bad file descriptor");
	    case PALMERR_EOF:
		return N_("End of file");
	    case PALMERR_ABORT:
		return N_("Transfer aborted");
	    case PALMERR_BADID:
		return N_("Invalid request ID");
	    case PALMERR_BADRESID:
		return N_("Invalid result ID");
	    case PALMERR_BADARGID:
		return N_("Invalid argument ID");
	    case PALMERR_ACKXID:
		return N_("XID on ACK doesn't match request");
	}

	/* This should never be reached */
	return N_("Unknown error");
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */