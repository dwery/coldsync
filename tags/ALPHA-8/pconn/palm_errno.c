/* palm_errno.c
 *
 * Error-related stuff for libpalm.
 *
 * $Id: palm_errno.c,v 1.3 1999-06-24 02:47:16 arensb Exp $
 */
#include "palm_errno.h"

int palm_errno;				/* Current error code */

/* XXX - Need something to keep this array and the error codes in sync */
/* XXX - I18N */
const char *palm_errlist[] = {		/* Error messages */
	"No error",
	"Error in system call or library function",
	"Out of memory",
	"Timeout",
	"Bad file descriptor",
	"End of file",
	"Transfer aborted",
	"Invalid request ID",
	"Invalid result ID",
	"Invalid argument ID",
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