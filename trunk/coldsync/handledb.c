/* handledb.c
 *
 * Figure out what to do with a database on the Palm.
 *
 * $Id: handledb.c,v 1.7 1999-06-27 05:57:58 arensb Exp $
 */

#include <stdio.h>
#include <unistd.h>		/* For chdir() */
#include <stdlib.h>		/* For getenv() */
#include <string.h>		/* For strncpy() and friends */
#include <sys/param.h>		/* For MAXPATHLEN */
#include <sys/types.h>		/* For stat() */
#include <sys/stat.h>		/* For stat() */
#include "config.h"
#include "coldsync.h"
#include "pconn/dlp_cmd.h"
#include "pdb.h"
#include "conduit.h"

/* HandleDB
 * Sync database number 'dbnum' with the desktop. If the database doesn't
 * exist on the desktop, 
 */
int
HandleDB(struct PConnection *pconn,
	 struct Palm *palm,
	 const int dbnum)
{
	int err;
	struct dlp_dbinfo *dbinfo;	/* Info about the database we're
					 * trying to handle. */
	static char bakfname[MAXPATHLEN];
					/* Filename to sync with */
	struct stat statbuf;		/* For finding out whether the
					 * file exists */
	struct pdb *localdb;		/* Local copy of the database */
	const struct conduit_spec *conduit;
					/* Conduit for this database */

	dbinfo = &(palm->dblist[dbnum]);	/* Convenience pointer */

	/* See if there's a conduit for this database. If so, invoke it and
	 * let it do all the work.
	 */
	if ((conduit = find_conduit(dbinfo)) != NULL)
	{
		fprintf(stderr, "Found a conduit for \"%s\"\n",
			dbinfo->name);
		err = (*(conduit->run))(pconn, palm, dbinfo);
		fprintf(stderr, "Conduit returned %d\n", err);
		return err;
	}

	/* If we get this far, then we need to do a generic backup or sync.
	 * XXX - Actually, the remainder of this function should *be* the
	 * generic sync.
	 */

	/* XXX - Use 'backupdir', from coldsync.h */
	/* XXX - This directory creation ought to happen fairly early on,
	 * probably as soon as it is determined whose Palm is being synced.
	 */
	if ((err = chdir(getenv("HOME"))) < 0)
	{
		fprintf(stderr, "Can't cd to home directory. Check $HOME.\n");
		perror("chdir($HOME)");
		return -1;
	}
	if ((err = chdir(".palm")) < 0)
	{
		fprintf(stderr, "Can't cd to ~/.palm\n");
		perror("chdir(~/.palm)");
		return -1;
	}
	if ((err = chdir("backup")) < 0)
	{
		fprintf(stderr, "Can't cd to ~/.palm/backup\n");
		perror("chdir(~/.palm/backup)");
		return -1;
	}

	/* XXX - See if the database file exists. If not, do a backup and
	 * return.
	 */
	/* Construct the backup filename */
	sprintf(bakfname, "%s.%s", dbinfo->name,
		DBINFO_ISRSRC(dbinfo) ? "prc" : "pdb");

	/* Stat the backup filename */
	err = stat(bakfname, &statbuf);
	if (err < 0)
	{
		/* XXX - Should check errno */

		if (DBINFO_ISROM(dbinfo))
		{
			printf("\"%s\" is a ROM database. Not backing it up.\n",
			       dbinfo->name);
			return 0;
		}

		/* The file doesn't exist. Back it up */
		printf("\tNeed to do a backup\n");
		return Backup(pconn, palm, dbinfo, bakfname);

		/* NOTREACHED */
	}

	/* If we get this far, then the backup filename does exist */

	/* XXX - How do we want to deal with resource files, which aren't
	 * really addressed by the Pigeon Book's stuff on syncing? On one
	 * hand, you're really not expected to sync resource files the same
	 * way as record files, and there's really no support for it; on
	 * the other hand, it could be nifty, and ought to be done for
	 * completeness.
	 * Perhaps the sane way to do it is to check the database type: if
	 * it's "appl", then it's an application, so don't sync it.
	 * Otherwise, if the sync type is either "desktop overwrites
	 * handheld" or "handheld overwrites desktop", then sync it that
	 * way. Otherwise, if there's a conduit for that type of database,
	 * use that conduit. Otherwise, just ignore it.
	 */
	/* XXX - For now, just ignore resource databases */
	if (DBINFO_ISRSRC(dbinfo))
	{
		fprintf(stderr, "\"%s\" I don't sync resource files (yet)\n",
			dbinfo->name);
		return 0;
	}

	/* Load the local copy of the database file. */
	if ((localdb = pdb_Read(bakfname)) == NULL)
	{
		fprintf(stderr, "Can't load \"%s\"\n", bakfname);
		return -1;
	}

	/* Do either a fast or a slow sync, as necessary.
	 * XXX - This probably needs to be maintained for each database
	 * separately.
	 */
	if (need_slow_sync)
		err = SlowSync(pconn, dbinfo, localdb, bakfname);
	else
		err = FastSync(pconn, dbinfo, localdb, bakfname);
	if (err < 0)
	{
		fprintf(stderr, "### SlowSync returned %d\n", err);
		free_pdb(localdb);
		return -1;
	}

	free_pdb(localdb);
	return 0;
}

/* mkbakfname
 * Given a database, construct the standard backup filename for it, and
 * return a pointer to it.
 * The caller need not free the string. OTOH, if ve wants to do anything
 * with it later, ve needs to make a copy.
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
