/* palment.c
 *
 * Functions for reading /etc/palms file.
 *
 *	Copyright (C) 2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: palment.c,v 2.1 2001-02-16 11:05:27 arensb Exp $
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>		/* For strtoul() */
#include <limits.h>		/* For strtoul() */
#include <string.h>		/* For strchr() */
#include "palment.h"

/* The functions in this file try to look like the getpwent(),
 * getgrent(), getnetent(), get*ent() ... families of functions, and
 * so pretty much assume single-threadedness: there are static
 * variables to store state, and Bad Things will happen if you try to
 * read /etc/palms in more than one thread at a time.
 *
 * The accessor functions getpalm*() return a pointer to a 'struct
 * palment'. This should be considered read-only and volatile: if you
 * want to retain any of this information, make a local copy before
 * the next call to a function in this file.
 */

#define MAXPALMENT	1024

static FILE *palmsfd = NULL;		/* File handle for /etc/palms */
static char entbuf[MAXPALMENT];		/* Buffer to hold one
					 * /etc/palms entry */
static struct palment cur_entry;	/* Current /etc/palms entry */

/* clear_palment
 * Helper function: clear 'ent' to empty values.
 */
static void
clear_palment(struct palment *ent)
{
	ent->serial = NULL;
	ent->username = NULL;
	ent->userid = 0L;
	ent->luser = NULL;
	ent->name = NULL;
	ent->conf_fname = NULL;
}

/* getpalment
 * Get the next /etc/palms entry (opening /etc/palms if necessary).
 * Fill in a 'struct palment' with the information, and return a
 * read-only "volatile" pointer to this information.
 *
 * Returns NULL in case of error, or end of file. Currently, there is
 * no way to distinguish between eof and abnormal termination.
 */
const struct palment *
getpalment(void)
{
	char *p;
	char *next;

	if (palmsfd == NULL)
	{
		/* Open /etc/palms */
		if ((palmsfd = fopen(_PATH_PALMS, "r")) == NULL)
			return NULL;
	}

	/* Read a line */
  readline:
	p = fgets(entbuf, sizeof(entbuf), palmsfd);
	if (p == NULL)
		return NULL;

	/* Parse the line */

	if (entbuf[0] == '\n')		/* Special case to ignore blank
					 * lines */
		goto readline;

	/* Chop off trailing '\n', if any */
	p = strchr(entbuf, '\n');
	if (p != NULL)
		*p = '\0';
		/* XXX - What if p == NULL? that would indicate that entbuf
		 * does not contain a \n, which in turn indicates that
		 * fgets() didn't read the entire line, which means that
		 * there's still a chunk of line waiting to be read.
		 */

	if (entbuf[0] == '#')		/* Skip comments */
		goto readline;

	clear_palment(&cur_entry);

	/* Break the line up into "|"-separated fields. */
	/* XXX - At least the first three fields (serial, username, userid)
	 * should be mandatory. If they're missing, getpalment() should
	 * return NULL and indicate an error somehow.
	 */

	/* Serial number */
	cur_entry.serial = entbuf;
	if ((next = strchr(entbuf, '|')) == NULL)
		goto done;
	*next = '\0';
	p = next+1;

	/* User name */
	cur_entry.username = p;
	if ((next = strchr(p, '|')) == NULL)
		goto done;
	*next = '\0';
	p = next+1;

	/* User ID */
	cur_entry.userid = strtoul(p, NULL, 10);
			/* Need to use strtoul() instead of atol() here,
			 * because 'userid' is an unsigned long; atol()
			 * barfs on values above 1<<31.
			 */
	if ((next = strchr(p, '|')) == NULL)
		goto done;
	p = next+1;

	/* Unix user name (or uid) */
	cur_entry.luser = p;
	if ((next = strchr(p, '|')) == NULL)
		goto done;
	*next = '\0';
	p = next+1;

	/* Palm name */
	cur_entry.name = p;
	if ((next = strchr(p, '|')) == NULL)
		goto done;
	*next = '\0';
	p = next+1;

	/* Config file name */
	cur_entry.conf_fname = p;
	if ((next = strchr(p, '|')) == NULL)
		goto done;
	*next = '\0';
	p = next+1;

  done:
	return &cur_entry;
}

/* setpalment
 * Reset state, so that the next call to getpalment() will return the
 * first entry in /etc/palms.
 * XXX - 'stayopen' is currently ignored. Is this a bad thing?
 */
void
setpalment(int stayopen)
{
	/* Close 'palmsfd' if it was already open.
	 * Don't just rewind to the beginning with lseek(): it could
	 * be that the file was deleted and replaced since the last
	 * time it was opened, and we want to catch this.
	 */
	if (palmsfd != NULL)
		fclose(palmsfd);

	/* (Re)open /etc/palms */
	palmsfd = fopen(_PATH_PALMS, "r");
}

/* endpalment
 * Close /etc/palms. Reset state.
 */
void
endpalment(void)
{
	/* Close palmsfd if necessary */
	if (palmsfd != NULL)
	{
		fclose(palmsfd);
		palmsfd = NULL;
	}
}

/* This is for Emacs's benefit:
 * Local Variables:	***
 * fill-column:	75	***
 * End:			***
 */
