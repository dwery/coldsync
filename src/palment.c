/* palment.c
 *
 * Functions for reading /etc/palms file.
 *
 *	Copyright (C) 2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: palment.c,v 2.3 2002-04-17 23:18:34 azummo Exp $
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>		/* For strtoul() */
#include <limits.h>		/* For strtoul() */
#include <string.h>		/* For strchr() */
#include "coldsync.h"
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


/* find_palment
 * When given a serial numer, an username and an userid, this routine
 * tries to find a matching palment entry.
 */

struct palment *
find_palment(const char *p_snum, const char *p_username, const udword p_userid, const ubyte match_type)
{
	struct palment *palment;

	while ((palment = getpalment()) != NULL)
	{
		char entserial[SNUM_MAX];	/* Serial number from
						 * /etc/palms, but without
						 * the checksum.
						 */
		char *dashp;			/* Pointer to "-" */

		/* Get the serial number, but without the checksum */
		/* XXX - Actually, this should look for the pattern
		 *	/-[A-Z]$/
		 * since in the future, there might be a special serial
		 * number like "*Visor-Plus*" which would match.
		 */
		if (palment->serial != NULL)
		{
			strncpy(entserial, palment->serial, SNUM_MAX);
			dashp = strrchr(entserial, '-');
			if (dashp != NULL)
				*dashp = '\0';
		} else {
			entserial[0] = '\0';
		}

		SYNC_TRACE(3)
			fprintf(stderr,
				" Evaluating serial [%s], username [%s], "
				"userid %lu\n",
				palment->serial, palment->username,
				palment->userid);

		if (match_type & PMATCH_SERIAL)
		{
			if (strncasecmp(entserial, p_snum, SNUM_MAX) != 0)
			{
				SYNC_TRACE(4)
					fprintf(stderr,
						" Serial number [%s] doesn't match with [%s].\n",
						palment->serial, p_snum);
				continue;
			}

			SYNC_TRACE(5)
				fprintf(stderr, " Serial number [%s] matches with [%s].\n",
					palment->serial, p_snum);
		}
		else
			SYNC_TRACE(5)
				fprintf(stderr, " serial match not required.\n");

		if (match_type & PMATCH_USERNAME)
		{
			if ((palment->username != NULL) &&
			    (palment->username[0] != '\0') &&
			    strncmp(palment->username, p_username,
				    DLPCMD_USERNAME_LEN) != 0)
			{
				SYNC_TRACE(4)
					fprintf(stderr,
						" Username [%s] doesn't match\n",
						palment->username);
				continue;
			}

			SYNC_TRACE(5)
				fprintf(stderr, " Username [%s] matches\n",
					palment->username);
		}
		else
			SYNC_TRACE(5)
				fprintf(stderr, " username match not required.\n");

		if (match_type & PMATCH_USERID)
		{
			if (palment->userid != p_userid)
			{
				SYNC_TRACE(4)
					fprintf(stderr,
						" Userid %lu doesn't match %lu\n",
						palment->userid,
						p_userid);
				continue;
			}

			SYNC_TRACE(5)
				fprintf(stderr, " Userid %lu matches\n",
					palment->userid);
		}
		else
			SYNC_TRACE(5)
				fprintf(stderr, " username match not required.\n");


		SYNC_TRACE(3)
			fprintf(stderr,
				"Found a match: luser [%s], name [%s], "
				"conf_fname [%s]\n",
				palment->luser, palment->name, palment->conf_fname);

		break;	/* If we get this far, this entry matches */
	}
	endpalment();

	return palment;
}

/* This is for Emacs's benefit:
 * Local Variables:	***
 * fill-column:	75	***
 * End:			***
 */
