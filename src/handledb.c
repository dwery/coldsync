/* handledb.c
 *
 * Figure out what to do with a database on the Palm.
 *
 *	Copyright (C) 1999, 2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: handledb.c,v 1.13 2000-01-27 02:40:16 arensb Exp $
 */

#include "config.h"
#include <stdio.h>
#include <string.h>		/* For strncpy() and friends */
#include <sys/param.h>		/* For MAXPATHLEN */
#include <ctype.h>		/* For isprint() and friends */

#if HAVE_LIBINTL
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL */

#include "coldsync.h"
#include "pconn/pconn.h"
#include "pdb.h"
#include "conduit.h"

extern int run_GenericConduit(struct PConnection *pconn,
			      struct Palm *palm,
			      struct dlp_dbinfo *db);

/* HandleDB
 * Sync database number 'dbnum' with the desktop.
 */
int
HandleDB(struct PConnection *pconn,
	 struct Palm *palm,
	 const int dbnum)
{
	int err;
	struct dlp_dbinfo *dbinfo;	/* Info about the database we're
					 * trying to handle. */

	dbinfo = &(palm->dblist[dbnum]);	/* Convenience pointer */

	SYNC_TRACE(1)
		fprintf(stderr, "Syncing %s\n",
			dbinfo->name);

	if (DBINFO_ISRSRC(dbinfo))
	{
/* XXX - Crud. There's no function to back up a single database */
#if 0
		struct stat statbuf;	/* For stat(), to see if the backup
					 * file exists.
					 */
		const char *bakfname;

		/* See if the backup file exists */
		bakfname = mkbakfname(dbinfo->name);
		err = lstat(bakfname, &statbuf);
		if ((err < 0) && (errno == ENOENT))
		{
			SYNC_TRACE(2)
				fprintf(stderr, "%s doesn't exist. Doing a "
					"backup\n",
					bakfname);
			err = Backup(pconn, palm);
			MISC_TRACE(2)
				fprintf(stderr, "Backup() returned %d\n", err);
		}
#endif	/* 0 */

		SYNC_TRACE(2)
			fprintf(stderr,
				"HandleDB: \"%s\": I don't deal with "
				"resource databases (yet).\n",
			dbinfo->name);
		/* XXX - If the backup file doesn't exist, make a backup */
/*  		return -1; */
		return 0;
	}

	/* XXX - This should walk through the list of 'Sync'-flavored
	 * conduits. If the path matches /^<.*>$/, then that refers to a
	 * built-in conduit. If none was found, use run_GenericConduit().
	 */
	err = run_GenericConduit(pconn, palm, dbinfo);
	SYNC_TRACE(3)
		fprintf(stderr, "GenericConduit returned %d\n", err);
	return err;
}

/* XXX - Should also have, mkatticfname()(?), and function to convert from
 * escaped names, so can check whether something goes in the attic.
 */

/* mkfname
 * Append the name of `dbinfo' to the directory `dirname', escaping any
 * weird characters so that the result can be used as a pathname.
 *
 * If `add_suffix' is true, appends ".pdb" or ".prc", as appropriate, to
 * the filename.
 *
 * Returns a pointer to the resulting filename.
 */
const char *
mkfname(const char *dirname,
	const struct dlp_dbinfo *dbinfo,
	Bool add_suffix)
{
	static char buf[MAXPATHLEN+1];
	static char namebuf[(DLPCMD_DBNAME_LEN * 3) + 1];
				/* Buffer to hold the converted name (with
				 * all of the weird characters escaped).
				 * Since an escaped character is 3
				 * characters long, this buffer need only
				 * be 3 times the max. length of the
				 * database name (plus one for the \0).
				 */
	int i;
	char *nptr;

	MISC_TRACE(3)
		fprintf(stderr, "Inside mkfname(\"%s\",\"%s\")\n",
			dirname, dbinfo->name);
	strncpy(buf, dirname, MAXPATHLEN);
	strncat(buf, "/", MAXPATHLEN-strlen(buf));

	/* If there are any weird characters in the database's name, escape
	 * them before trying to create a file by that name. Any "weird"
	 * characters are replaced by "%HH", where HH is the ASCII value
	 * (hex) of the weird character.
	 */
	nptr = namebuf;
	for (i = 0; i < DLPCMD_DBNAME_LEN; i++)
	{
		if (dbinfo->name[i] == '\0')
			break;

		/* XXX - The isgraph() is mainly for testing. isprint() is
		 * a better test; spaces are allowed in filenames, and it
		 * makes the result much more readable.
		 */
		if ((!/*isprint*/isgraph(dbinfo->name[i])) ||
		    (dbinfo->name[i] == '/') ||	/* '/' is a weird character */
		    (dbinfo->name[i] == '%'))	/* The escape character
						 * needs to be escaped.
						 */
		{
			/* Escape it */
			sprintf(nptr, "%%%02x", dbinfo->name[i]);
			nptr += 3;
		} else {
			/* Just a regular character */
			*nptr = dbinfo->name[i];
			nptr++;
		}
	}
	*nptr = '\0';
	strncat(buf, namebuf, MAXPATHLEN-(nptr-namebuf));

	if (add_suffix)
		strncat(buf, (DBINFO_ISRSRC(dbinfo) ? ".prc" : ".pdb"),
			MAXPATHLEN-strlen(buf));

	MISC_TRACE(3)
		fprintf(stderr, "mkfname:    -> \"%s\"\n", buf);
	return buf;
}

/* mkbakfname
 * Given a database, construct the standard backup filename for it, and
 * return a pointer to it.
 * The caller need not free the string. OTOH, if ve wants to do anything
 * with it later, ve needs to make a copy.
 * This isn't a method in GenericConduit because it's generic enough that
 * other conduits might want to make use of it.
 */
const char *
mkbakfname(const struct dlp_dbinfo *dbinfo)
{
	return mkfname(backupdir, dbinfo, True);
}

const char *
mkarchfname(const struct dlp_dbinfo *dbinfo)
{
	return mkfname(archivedir, dbinfo, False);
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
