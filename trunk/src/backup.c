/* backup.c
 *
 * Functions for backing up Palm databases (both .pdb and .prc) from
 * the Palm to the desktop.
 *
 * $Id: backup.c,v 2.1 1999-07-12 09:20:04 arensb Exp $
 */
#include <stdio.h>
#include <fcntl.h>		/* For open() */
#include <string.h>		/* For strncpy(), strncat() */
#include <ctype.h>		/* For isprint() */
#include "PConnection.h"
#include "pdb.h"
#include "coldsync.h"

/* XXX - Should this only back up one file, by analogy with conduits? */
int
Backup(struct PConnection *pconn,
       struct Palm *palm)
{
	int err;
	int i;

	SYNC_TRACE(1)
		fprintf(stderr, "Inside Backup() -> \"%s\"\n",
			global_opts.backupdir);

	for (i = 0; i < palm->num_dbs; i++)
	{
		struct dlp_dbinfo *dbinfo;
		static char bakfname[MAXPATHLEN+1];	/* Backup file name */
		int bakfd;		/* Backup file descriptor */
		struct pdb *pdb;	/* Database downloaded from Palm */
		ubyte dbh;		/* Database handle (on Palm) */

		dbinfo = &(palm->dblist[i]);

		/* Construct the backup file name */
		/* XXX - This can be optimized: only write the backup
		 * directory and slash once.
		 */
		/* XXX - Watch out for weird characters in database name: I
		 * guess they should be replaced either with underscores
		 * or, better, %HH, where HH is the hex value of the
		 * character.
		 * Slashes aren't allowed. Neither are NULs, but they're a
		 * separate issue. I guess 'isprint()' is a good enough
		 * test for allowable characters (of course, '%' should be
		 * escaped, too).
		 */
		strncpy(bakfname, global_opts.backupdir, MAXPATHLEN);
		strncat(bakfname, "/", MAXPATHLEN - strlen(bakfname));
		strncat(bakfname, dbinfo->name, MAXPATHLEN - strlen(bakfname));
		if (DBINFO_ISRSRC(dbinfo))
			strncat(bakfname, ".prc",
				MAXPATHLEN - strlen(bakfname));
		else
			strncat(bakfname, ".pdb",
				MAXPATHLEN - strlen(bakfname));

		/* XXX - This is logging, not debugging */
		SYNC_TRACE(2)
			fprintf(stderr, "Backing up \"%s\" to \"%s\"\n",
				dbinfo->name, bakfname);

		/* Create and open the backup file */
		/* XXX - Is the O_EXCL flag desirable? */
		bakfd = open(bakfname, O_WRONLY | O_CREAT | O_EXCL, 0600);
		if (bakfd < 0)
		{
			fprintf(stderr, "Backup: can't create new backup file %s\n"
				"It may already exist.\n",
				bakfname);
			perror("open");
			return -1;
		}
		/* XXX - Lock the file */

		/* Open the database on the Palm */
		err = DlpOpenConduit(pconn);
		if (err != DLPSTAT_NOERR)
		{
			fprintf(stderr, "Can't open backup conduit.\n");
			close(bakfd);
			return -1;
		}

		err = DlpOpenDB(pconn,
				CARD0,
				dbinfo->name,
				DLPCMD_MODE_READ |
				(dbinfo->db_flags & DLPCMD_DBFLAG_OPEN ?
				 0 :
				 DLPCMD_MODE_WRITE) |
				DLPCMD_MODE_SECRET,
				/* "Secret" records aren't actually secret.
				 * They're actually just the ones marked
				 * private, and they're not at all secret.
				 */
				&dbh);
		if (err != DLPSTAT_NOERR)
		{
			fprintf(stderr, "Can't open database \"%s\"\n",
				dbinfo->name);
			close(bakfd);
			return -1;
		}

		/* Download the database from the Palm */
		pdb = pdb_Download(pconn, dbinfo, dbh);
		SYNC_TRACE(7)
			fprintf(stderr, "After pdb_Download\n");
		DlpCloseDB(pconn, dbh);
		SYNC_TRACE(7)
			fprintf(stderr, "After DlpCloseDB\n");

		/* Write the database to the backup file */
		err = pdb_Write(pdb, bakfd);
		if (err < 0)
		{
			fprintf(stderr, "Backup: can't write database \"%s\" to \"%s\"\n",
				dbinfo->name, bakfname);
			close(bakfd);
			return -1;
		}
		SYNC_TRACE(3)
			fprintf(stderr, "Wrote \"%s\" to \"%s\"\n",
				dbinfo->name, bakfname);

		close(bakfd);
	}
	return 0;
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
