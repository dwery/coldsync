/* handledb.c
 *
 * Figure out what to do with a database on the Palm.
 *
 *	Copyright (C) 1999, 2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: handledb.c,v 1.12 2000-01-22 05:16:00 arensb Exp $
 */

#include "config.h"
#include <stdio.h>
#include <string.h>		/* For strncpy() and friends */
#include <sys/param.h>		/* For MAXPATHLEN */

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
	static char buf[MAXPATHLEN+1];

	strncpy(buf, backupdir, MAXPATHLEN);
	strncat(buf, "/", MAXPATHLEN-strlen(buf));
	strncat(buf, dbinfo->name, MAXPATHLEN-strlen(buf));
	strncat(buf, (DBINFO_ISRSRC(dbinfo) ? ".prc" : ".pdb"),
		MAXPATHLEN-strlen(buf));
	return buf;
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
