/* log.c
 *
 * Convenience functions for logging.
 *
 * $Id: log.c,v 1.5 1999-08-25 09:51:54 arensb Exp $
 */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>		/* For realloc() */
#include <string.h>		/* For strcat() */
#include "coldsync.h"

extern char *synclog;
extern int log_size;
extern int log_len;

/* add_to_log
 * Unfortunately, the DlpAddSyncLogEntry() function's name is
 * misleading: it doesn't allow you to add to the sync log; rather, it
 * allows you to write a single log message.
 * The add_to_log() function gets around this: it allows you to build
 * up a log entry one string at a time. At the end of the sync, the
 * entire log will be written to the Palm.
 */
/* XXX - This setup is extremely ugly, what with the log-related
 * variables scattered amongst several files and whatnot. Perhaps a
 * better setup would be to have three functions:
 *	log_t *open_synclog(struct PConnection *);
 *	int sync_log(log_t *, char *msg);
 *	int close_synclog(log_t *);
 * How best to do this? A log is really associated with a given sync,
 * not a connection, really. Perhaps it'd be best to add some fields
 * to 'struct Palm'?
 */
int
add_to_log(char *msg)
{
	SYNC_TRACE(5)
		fprintf(stderr, "add_to_log(\"%s\")\n", msg);
	if (strlen(msg) + log_len >= log_size)
	{
		char *newlog;
		int newsize = log_size;

		if (log_size == 0)
			newsize = 1024;
		while (newsize < strlen(msg) + log_len)
			newsize *= 2;

		/* (Re)allocate memory for log */
		if (log_size == 0)
			newlog = malloc(newsize);
		else
			newlog = realloc(synclog, newsize);

		if (newlog == NULL)
		{
			fprintf(stderr, "add_to_log: realloc failed\n");
			perror("realloc");
			return -1;
		}
		if (log_size == 0)
			newlog[0] = '\0';	/* Terminate the
						 * newly-allocated string
						 */
		synclog = newlog;
		log_size = newsize;
	}
	strcat(synclog, msg);
	SYNC_TRACE(5)
		fprintf(stderr, "Now log is \"%s\"\n", synclog);
	return 0;
}
