/* backup.c
 *
 * Functions for backing up Palm databases (both .pdb and .prc) from
 * the Palm to the desktop.
 *
 * $Id: backup.c,v 1.6 1999-03-11 04:13:40 arensb Exp $
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>		/* For open() */
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <string.h>		/* For memcpy() et al. */
#include "config.h"
#include "palm/palm_types.h"
#include "pconn/palm_errno.h"
#include "pconn/dlp_cmd.h"
#include "pconn/util.h"
#include "pdb.h"
#include "coldsync.h"

int
Backup(struct PConnection *pconn,
       struct Palm *palm,
       struct dlp_dbinfo *dbinfo,
       char *bakfname)
{
	int err;
	struct pdb *db;
	ubyte dbh;		/* Database handle */

printf("--> Backing up database %s to %s\n",
       dbinfo->name, bakfname);

	/* Tell the Palm we're synchronizing a database */
	err = DlpOpenConduit(pconn);
	if (err != DLPSTAT_NOERR)
		return -1;

	/* Open the database */
	/* XXX - Card #0 shouldn't be hardwired. It probably ought to be a
	 * field in dlp_dbinfo or something.
	 */
	err = DlpOpenDB(pconn, 0, dbinfo->name,
			DLPCMD_MODE_READ |
			(dbinfo->db_flags & DLPCMD_DBFLAG_OPEN ?
			 0 :
			 DLPCMD_MODE_WRITE) |
				/* If the database is already open, don't
				 * open it for writing.
				 */
			DLPCMD_MODE_SECRET,
				/* XXX - Should there be a flag to
				 * determine whether we read secret records
				 * or not? */
			&dbh);
	switch (err)
	{
	    case DLPSTAT_NOERR:
		/* Things are fine */
		break;
	    case DLPSTAT_NOTFOUND:
	    case DLPSTAT_TOOMANYOPEN:
	    case DLPSTAT_CANTOPEN:
		/* Can't complete this particular operation, but it's not a
		 * show-stopper. The sync can go on.
		 */
		fprintf(stderr, "Backup: Can't open \"%s\": %d\n",
			dbinfo->name, err);
		return -1;
	    default:
		/* Some other error, which probably means the sync can't
		 * continue.
		 */
		fprintf(stderr, "Backup: Can't open \"%s\": %d\n",
			dbinfo->name, err);
		return -1;
	}

	/* XXX - Set the various record flags to sane values: none of them
	 * should be dirty or needing archiving.
	 */
	db = pdb_Download(pconn, dbinfo, dbh);
	if (db == NULL)
	{
		fprintf(stderr, "Can't download \"%s\"\n", dbinfo->name);
		DlpCloseDB(pconn, dbh);
		return -1;
	}

	/* XXX - Error-checking */
	pdb_Write(db, bakfname);

	/* XXX - I'm not sure about the relative order of cleaning the
	 * database and resetting the sync flags. HotSync appears to do
	 * both at different times.
	 */
	if (!DBINFO_ISRSRC(dbinfo))
	{
		/* Resource databases don't get cleaned. */
fprintf(stderr, "### Cleaning up database\n");
		err = DlpCleanUpDatabase(pconn, dbh);
		if (err != DLPSTAT_NOERR)
		{
			fprintf(stderr, "Can't clean up database: err = %d\n",
				err);
			DlpCloseDB(pconn, dbh);
			free_pdb(db);
			return -1;
		}
	}

fprintf(stderr, "### Resetting sync flags 1\n");
	err = DlpResetSyncFlags(pconn, dbh);
	if (err != DLPSTAT_NOERR)
	{
		fprintf(stderr, "Can't reset sync flags: %d\n", err);
		DlpCloseDB(pconn, dbh);
		free_pdb(db);
		return -1;
	}

	/* Clean up */
	DlpCloseDB(pconn, dbh);		/* Close the database */
	free_pdb(db);

	return 0;		/* Success */
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
