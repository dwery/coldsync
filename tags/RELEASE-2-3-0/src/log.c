/* log.c
 *
 * Convenience functions for logging.
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: log.c,v 1.24 2001-10-12 03:59:40 arensb Exp $
 */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>		/* For realloc() */
#include <string.h>		/* For strcat() */
#include <stdarg.h>		/* Variable-length argument lists */

#if HAVE_LIBINTL_H
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL_H */

#include "pconn/pconn.h"
#include "coldsync.h"

/* va_add_to_log
 * Takes a printf()-style-formatted log message, and sends it to the
 * Palm on 'pconn'.
 * Returns 0 if successful, or a negative value in case of error.
 */
/* XXX - DLPC_MAXLOGLEN is the maximum length for the entire log on
 * the Palm. If the current message would make the total log longer
 * than that, then the entire current message is dropped. It might be
 * nice to keep track of the log length so far, and truncate the
 * current message to that portion which will fit.
 */
int
va_add_to_log(PConnection *pconn, const char *fmt, ...)
{
	int err;
	va_list ap;
	static char buf[DLPC_MAXLOGLEN];

	/* Format and print the message to 'buf' */
	va_start(ap, fmt);
	err = vsnprintf(buf, DLPC_MAXLOGLEN, fmt, ap);

	SYNC_TRACE(4)
		fprintf(stderr,
			"va_add_to_log: vsnprintf() returned %d, "
			"buf == [%.*s]\n",
			err, DLPC_MAXLOGLEN, buf);
	if (err < 0)
		return err;

	/* Send the message to the Palm */
	return DlpAddSyncLogEntry(pconn, buf);
}

/* This is for Emacs's benefit:
 * Local Variables:	***
 * fill-column:	75	***
 * End:			***
 */
