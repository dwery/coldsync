/* handledb.c
 *
 * Figure out what to do with a database on the Palm.
 *
 *	Copyright (C) 1999, 2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: handledb.c,v 1.16 2000-01-27 07:38:48 arensb Exp $
 */

#include "config.h"
#include <stdio.h>
#include <sys/param.h>		/* For MAXPATHLEN */
#include <sys/types.h>		/* For stat() */
#include <sys/stat.h>		/* For stat() */
#include <ctype.h>		/* For isprint() and friends */
#include <errno.h>		/* For errno. Duh. */

#if STDC_HEADERS
# include <string.h>		/* For memcpy() et al. */
#else	/* STDC_HEADERS */
# ifndef HAVE_STRCHR
#  define strchr index
#  define strrchr rindex
# endif	/* HAVE_STRCHR */
# ifndef HAVE_MEMCPY
#  define memcpy(d,s,n)		bcopy ((s), (d), (n))
#  define memmove(d,s,n)	bcopy ((s), (d), (n))
# endif	/* HAVE_MEMCPY */
#endif	/* STDC_HEADERS */

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
		struct stat statbuf;	/* For stat(), to see if the backup
					 * file exists.
					 */
		const char *bakfname;

		/* See if the backup file exists */
		bakfname = mkbakfname(dbinfo);
		err = lstat(bakfname, &statbuf);
		if ((err < 0) && (errno == ENOENT))
		{
			SYNC_TRACE(2)
				fprintf(stderr, "%s doesn't exist. Doing a "
					"backup\n",
					bakfname);
			err = backup(pconn, dbinfo, backupdir);
			/* XXX - Error-checking */
			MISC_TRACE(2)
				fprintf(stderr, "backup() returned %d\n", err);
		}

		SYNC_TRACE(2)
			fprintf(stderr,
				"HandleDB: \"%s\": I don't deal with "
				"resource databases (yet).\n",
			dbinfo->name);
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

		/* Is this a weird character? */
		if ((!isprint((int) dbinfo->name[i])) ||
		    (dbinfo->name[i] == '/') ||	/* '/' is a weird character */
		    (dbinfo->name[i] == '%'))	/* The escape character
						 * needs to be escaped.
						 */
		{
			/* Escape it */
			sprintf(nptr, "%%%02X", dbinfo->name[i]);
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

/* hex2int
 * Quick and dirty utility function to convert a hex digit to its numerical
 * equivalent.
 */
static int
hex2int(const char hex)
{
	switch (hex)
	{
	    case '0':
	    case '1':
	    case '2':
	    case '3':
	    case '4':
	    case '5':
	    case '6':
	    case '7':
	    case '8':
	    case '9':
		return (int) hex - '0';
	    case 'a':
	    case 'b':
	    case 'c':
	    case 'd':
	    case 'e':
	    case 'f':
		return (int) hex - 'a' + 0xa;
	    case 'A':
	    case 'B':
	    case 'C':
	    case 'D':
	    case 'E':
	    case 'F':
		return (int) hex - 'A' + 0xa;
	    default:
		return -1;
	}
}

/* fname2dbname
 * Convert a file name to a database name: strip off the trailing ".prc" or
 * ".pdb", if any, and convert any %HH sequences back to their
 * corresponding values.
 * Returns a string with the database name, or NULL if the pathname does
 * not correspond to a database name.
 */
const char *
fname2dbname(const char *fname)
{
	int i;
	static char dbname[DLPCMD_DBNAME_LEN+1];
	const char *baseptr;		/* Pointer to last '/' in pathname */
	const char *dotptr;		/* Pointer to extension */

	MISC_TRACE(3)
		fprintf(stderr, "fname2dbname: Unescaping \"%s\"\n",
			fname);

	/* Clear the database name, just 'cos. */
	memset(dbname, '\0', DLPCMD_DBNAME_LEN+1);

	/* Find the beginning of the filename. This is either the character
	 * just after the last '/', or the beginning of the string.
	 */
	baseptr = strrchr(fname, '/');
	if (baseptr == NULL)
		/* No directories, just a plain filename */
		baseptr = fname;
	else
		baseptr++;

	/* Find the extension (last '.' after the '/') */
	dotptr = strrchr(baseptr, '.');
	if (dotptr == NULL)
		/* No dot. But there really should be a ".prc" or ".pdb"
		 * extension, so this isn't a database name.
		 */
		return NULL;

	/* See if the extension is ".prc" or ".pdb" */
	if ((strcasecmp(dotptr, ".prc") != 0) &&
	    (strcasecmp(dotptr, ".pdb") != 0))
		/* Bad extension. It's not a database name */
		return NULL;

	/* Copy the base name, removing escapes, to 'dbname' */
	for (i = 0; i < DLPCMD_DBNAME_LEN; i++)
	{
		if ((baseptr == dotptr) ||
		    (*baseptr == '\0'))
			break;	/* End of name */
				/* We should never get to the end of the
				 * string, mind.
				 */

		if (*baseptr == '%')
		{
			int first;	/* First hex digit */
			int second;	/* Second hex digit */

			/* Escaped character */

			if ((first = hex2int(baseptr[1])) < 0)
				/* Not a valid hex digit */
				return NULL;
			if ((second = hex2int(baseptr[2])) < 0)
				/* Not a valid hex digit */
				return NULL;

			dbname[i] = (first << 4) | second;
			baseptr += 3;

			continue;
		}

		/* Just a normal character */
		dbname[i] = *baseptr;
		baseptr++;
	}

	/* Return 'dbname' */
	MISC_TRACE(3)
		fprintf(stderr, "fname2dbname: Returning  \"%s\"\n",
			dbname);
	return dbname;
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
