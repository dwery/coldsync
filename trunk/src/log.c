/* log.c
 *
 * Convenience functions for logging.
 *
 *	Copyright (C) 1999, 2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: log.c,v 1.12 2000-02-06 22:12:42 arensb Exp $
 */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>		/* For realloc() */
#include <string.h>		/* For strcat() */

#if HAVE_LIBINTL
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL */

#include "coldsync.h"

char *synclog;			/* Log that'll be uploaded to the Palm */
static int log_size = 0;	/* Size of 'synclog' */
static int log_len = 0;		/* Length of log == strlen(synclog) */

/* add_to_log
 * Unfortunately, the DlpAddSyncLogEntry() function's name is
 * misleading: it doesn't allow you to add to the sync log; rather, it
 * allows you to write a single log message.
 * The add_to_log() function gets around this: it allows you to build
 * up a log entry one string at a time. At the end of the sync, the
 * entire log will be written to the Palm.
 */
/* XXX - This isn't the prettiest setup imaginable, what with the
 * global variables and all. Since a log is associated with a
 * particular sync, it might be better to add a 'log' field to 'struct
 * Palm'.
 */
int
add_to_log(char *msg)
{
	int msglen;		/* Length of 'msg' */

	SYNC_TRACE(6)
		fprintf(stderr, "add_to_log(\"%s\")\n", msg);

	msglen = (synclog == NULL ? 0 : strlen(synclog));

	/* Increase the size of the log buffer, if necessary */
	if (log_len + msglen >= log_size)
	{
		char *newlog;
		int newsize;

		if (log_size == 0)
		{
			/* First time around. Need to allocate a new
			 * log buffer. Make it big enough for the
			 * current string, rounded up to the nearest
			 * Kb.
			 */
			newsize = (msglen + 1023) & 0x03ff;
				/* (msglen + 1023) % 1024 */

			if ((newlog = malloc(newsize)) == NULL)
			{
				fprintf(stderr, _("%s: Out of memory.\n"),
					"add_to_log");
				perror("malloc");
				return -1;
			}

			synclog = newlog;
			log_len = msglen;
			log_size = newsize;

		} else {
			/* Second through nth time around. The buffer
			 * already exists, but it's too small. Double
			 * its size. If that's still not enough,
			 * double it again, and so forth.
			 */
			newsize = log_size;
			while (log_len + msglen >= newsize)
				newsize *= 2;

			if ((newlog = realloc(synclog, newsize)) == NULL)
			{
				fprintf(stderr, _("%s: realloc failed\n"),
					"add_to_log");
				perror("realloc");
				return -1;
			}

			synclog = newlog;
			log_size = newsize;
		}
	}

	strcat(synclog, msg);
	log_len += msglen;

	SYNC_TRACE(4)
		fprintf(stderr, "Now log is \"%s\"\n", synclog);
	return 0;
}
