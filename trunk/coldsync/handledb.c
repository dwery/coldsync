/* handledb.c
 *
 * Figure out what to do with a database on the Palm.
 *
 * $Id: handledb.c,v 1.6 1999-05-31 21:05:57 arensb Exp $
 */

#include <stdio.h>
#include <unistd.h>		/* For chdir() */
#include <stdlib.h>		/* For getenv() */
#include <sys/param.h>		/* For MAXPATHLEN */
#include <sys/types.h>		/* For stat() */
#include <sys/stat.h>		/* For stat() */
#include "config.h"
#include "coldsync.h"
#include "pconn/dlp_cmd.h"
#include "pdb.h"

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

	dbinfo = &(palm->dblist[dbnum]);	/* Convenience pointer */

	/* XXX - See if there's a conduit for this database. If so, invoke
	 * it and let it do all the work.
	 */

	/* If we get this far, then we need to do a generic backup or sync.
	 * XXX - Actually, the remainder of this function should *be* the
	 * generic sync.
	 */

	/* XXX - Use 'backupdir', from coldsync.h */
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
		return -1;
	}

	return 0;
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
