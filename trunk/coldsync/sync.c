/* sync.c
 *
 * Functions for synching a database on the Palm with one one the
 * desktop.
 *
 * $Id: sync.c,v 1.1 1999-03-11 03:26:24 arensb Exp $
 */

#include <stdio.h>
#include "pconn/PConnection.h"
#include "pconn/dlp_cmd.h"
#include "coldsync.h"
#include "pdb.h"

/* XXX - According to the Pigeon Book, here's the logic of syncing:

   - If this machine is not the one that the Palm last synced with
   (LastSyncPC), need to do a slow sync first.

   * Slow sync: can't trust the modification time or flags on the Palm, so
	need to compare with the backup directory. Copy all of the Palm's
	records to the desktop, to a "remote" database. Then, for each
	record in the remote database:

	- If the remote record matches one in the backup database, nothing
	has changed.
	- If the remote record doesn't exist in the backup database, the
	record is new and is marked as modified (in the remote database,
	presumably).
	- If the backup record doesn't exist in the remote database, the
	remote record has been delete it. Mark it as deleted (in the remote
	database?) (If it was supposed to be archived, presumably it got
	archived somewhere else.)
	- If the backup and remote records don't match, the remote record
	has been modified; mark it as such.

   * Fast sync: for each modified record in the remote database (see
	above):

	- If the record is archived, add it to the local archive database;
	mark it in the local (backup?) database as pending deletion. Delete
	the record from the Palm.
	- If the record is deleted, mark it in the local database as
	pending deletion; delete it on the Palm.
	- If the record is modified, modify it in the local database.
	- If the record doesn't exist in the local database, it's new. Add
	it.

	For each record in the local database:

	- If the record is archived, delete it on the Palm; put it in the
	archive database; mark it as pending delete in the local database.
	- If the record is deleted, delete it on the Palm. Mark it as
	pending deletion in the local database.
	- If the record is modified, copy the changes to the Palm; clear
	the modification flag in the local database.
	- If the record is new, copy it to the Palm. Clear the added(?)
	flag in the local database.

	Possible conflicts: The rule is: don't lose data.

	- A record is deleted in one database and modified in the other:
	delete the deleted version of the record; copy the modified version
	to the other database.
	- A record is archived in one database and modified in the other:
	put the archived version in the archive database; copy the modified
	version to the other database. (But check to see if the data is the
	same: if so, don't need to do anything.)
	- A record is archived in one database and deleted in the other:
	put it in the archive database.
	- A record is changed in both databases: make two records.

   * Delete all records in the local database that are marked for deletion.

   * Write the local database to disk.

   * XXX - What about AppInfo and sort blocks in all this? Presumably, the
   * sort block doesn't matter, 'cos it's just for displaying information
   * on the Palm. As for the AppInfo block, use the AppInfoDirty (or
   * whatever) flag. Need to think this through, though: most of the stuff
   * that applies to records also applies to the AppInfo block, except the
   * case where it's been modified in both places, but not the same way: in
   * this case, you don't have the option of creating a second AppInfo
   * block.

 */

#if 0
/* XXX - This does a rather generic sync. This ought to either call a
 * conduit, or be replaced by a call to a conduit.
 */
int
Cold_RecordSync(struct PConnection *pconn,	/* Connection to Palm */
		struct Palm *palm,		/* Info about the Palm */
		struct dlp_dbinfo *dbinfo,	/* Info about the database */
		char *bakfname)			/* File to sync with */
{
	int err;
	struct pdb *db;
	ubyte dbh;
	int done;

fprintf(stderr, "--> Syncing record database %s with \"%s\"\n",
	dbinfo->name, bakfname);

	/* Load the database from disk */
	if ((db = LoadDatabase(bakfname)) == NULL)
	{
		fprintf(stderr, "Can't load database file \"%s\". "
			"Aborting sync.\n",
			bakfname);
		return -1;
	}

	/* Tell the Palm we're synchronizing a database */
	err = DlpOpenConduit(pconn);
	if (err != DLPSTAT_NOERR)
	{
		err = -1;
		goto abort;
	}

	/* Open the database */
	/* XXX - card #0 is hardwired */
	err = DlpOpenDB(pconn, 0, dbinfo->name,
			DLPCMD_MODE_READ |
			DLPCMD_MODE_WRITE /*| DLPCMD_MODE_SECRET*/,
			&dbh);
	switch (err)
	{
	    case DLPSTAT_NOERR:
		break;
	    default:
		fprintf(stderr, "*** Can't open %s: %d\n", dbinfo->name, err);
		err = -1;
		goto abort;
	}

	/* XXX - Read and update the AppInfo block */
	/* XXX - Read and update the sort block */
	/* XXX - DlpReadNextModifiedRec() */
	/* XXX - Actually, ought to find out whether this is a "slow" or a
	 * "fast" sync, first. If it's a "slow" sync, can't trust the sync
	 * flags, and need to read every record. Also, ought to do
	 * something intelligent wrt the creation/modification/backup times
	 */
	done = 0;
	while (!done)
	{
extern int dlpc_debug;
		struct dlp_recinfo recinfo;
		const ubyte *rec_data;

dlpc_debug = 10;
		err = DlpReadNextModifiedRec(pconn,
					     dbh,
					     &recinfo,
					     &rec_data);
		switch (err)
		{
		    case DLPSTAT_NOERR:
			break;
		    case DLPSTAT_NOTFOUND:
			/* XXX - Actually, at this point, it would make
			 * more sense to just jump to the end with a
			 * successful exit status.
			 */
			done = 1;	/* No more modified records */
			break;
		    default:
			fprintf(stderr, "##### DlpReadNextModifiedRec returned %d\n", err);
			err = -1;
			goto abort;
		}
	}

	/* CleanUpDatabase */
	if ((err = DlpCleanUpDatabase(pconn, dbh)) != DLPSTAT_NOERR)
	{
		fprintf(stderr, "##### Error cleaning up database\n");
		err = -1;
		goto abort;
	}

	/* XXX - HotSync calls ReadOpenDBInfo() and ReadRecordIDList() Why?
	 * Presumably to find out which records have been deleted without
	 * archiving and/or to see which ones have been added on the
	 * desktop?
	 */
	/* ResetSyncFlags */
	if ((err = DlpResetSyncFlags(pconn, dbh)) != DLPSTAT_NOERR)
	{
		fprintf(stderr, "##### Error resetting sync flags\n");
		err = -1;
		goto abort;
	}

  abort:
	/* Close the database */
	if (dbh != 0xffff)
		DlpCloseDB(pconn, dbh);	/* Don't really care if this fails */

	if (db != NULL)
		free_pdb(db);

	return err;
}
#endif	/* 0 */

int
SlowSync(struct PConnection *pconn,
	 struct dlp_dbinfo *remotedb,
	 struct pdb *localdb,
	 char *bakfname)
{
	int err;
	int i;
	struct pdb *db;		/* The database on the Palm will be read
				 * into here */
	ubyte dbh;		/* Database handle */

fprintf(stderr, "Doing a slow sync of \"%s\" to \"%s\" (filename \"%s\")\n",
	remotedb->name, localdb->header.name, bakfname);

	/* Open the database */
	/* XXX - Card #0 shouldn't be hardwired. It probably ought to be a
	 * field in dlp_dbinfo or something.
	 */
if (remotedb->db_flags & DLPCMD_DBFLAG_OPEN)
{
fprintf(stderr, "This database is open. Not opening for writing\n");
	err = DlpOpenDB(pconn, 0, remotedb->name,
			DLPCMD_MODE_READ |
			DLPCMD_MODE_SECRET,
				/* XXX - Should there be a flag to
				 * determine whether we read secret records
				 * or not? */
			&dbh);
} else {
fprintf(stderr, "This database isn't open. Opening for writing\n");
	err = DlpOpenDB(pconn, 0, remotedb->name,
			DLPCMD_MODE_READ |
			DLPCMD_MODE_WRITE |
			DLPCMD_MODE_SECRET,
				/* XXX - Should there be a flag to
				 * determine whether we read secret records
				 * or not? */
			&dbh);
}
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
		fprintf(stderr, "Cold_RecordBackup: Can't open \"%s\": %d\n",
			remotedb->name, err);
		return -1;
	    default:
		/* Some other error, which probably means the sync can't
		 * continue.
		 */
		fprintf(stderr, "Cold_RecordBackup: Can't open \"%s\": %d\n",
			remotedb->name, err);
		return -1;
	}

	/* Read the database on the Palm to an in-memory copy */
	if ((db = pdb_Download(pconn, remotedb, dbh)) == NULL)
	{
		fprintf(stderr, "Can't downlod \"%s\"\n",
			remotedb->name);
		DlpCloseDB(pconn, dbh);
		return -1;
	}

	/* XXX - Compare each record in turn to 'localdb' */
	/* XXX - This ought to handle both record and resource databases */
	for (i = 0; i < db->reclist_header.len; i++)
	{
		struct pdb_record *remoterec;
		struct pdb_record *localrec;

		remoterec = &(db->rec_index.rec[i]);

		printf("Remote Record %d:\n", i);
		printf("\tuniqueID: 0x%08lx\n", remoterec->uniqueID);
		printf("\tattributes: 0x%02x ",
		       remoterec->attributes);
		/* XXX - Need flag #defines */
		if (remoterec->attributes & 0x80)
			printf("DELETED ");
		if (remoterec->attributes & 0x40)
			printf("DIRTY ");
		if (remoterec->attributes & 0x20)
			printf("BUSY ");
		if (remoterec->attributes & 0x10)
			printf("SECRET ");
		if (remoterec->attributes & 0x08)
			printf("ARCHIVED ");
		printf("\n");

		/* Try to find this record in the local database */
		localrec = pdb_FindRecordByID(localdb, remoterec->uniqueID);
		if (localrec == NULL)
		{
			/* It doesn't exist. */
			/* XXX - Finish this comment */
			fprintf(stderr, "Can't find a local record with ID 0x%08lx\n",
				remoterec->uniqueID);
		}

else {
		printf("Local Record %d:\n", i);
		printf("\tuniqueID: 0x%08lx\n", localrec->uniqueID);
		printf("\tattributes: 0x%02x ",
		       localrec->attributes);
		/* XXX - Need flag #defines */
		if (localrec->attributes & 0x80)
			printf("DELETED ");
		if (localrec->attributes & 0x40)
			printf("DIRTY ");
		if (localrec->attributes & 0x20)
			printf("BUSY ");
		if (localrec->attributes & 0x10)
			printf("SECRET ");
		if (localrec->attributes & 0x08)
			printf("ARCHIVED ");
		printf("\n");
}
	}

if (remotedb->db_flags & DLPCMD_DBFLAG_OPEN)
{
	fprintf(stderr, "File is open. Not cleaning up, not resetting flags\n");
} else {
fprintf(stderr, "### Cleaning up database\n");
	err = DlpCleanUpDatabase(pconn, dbh);
	if (err != DLPSTAT_NOERR)
	{
		fprintf(stderr, "Can't clean up database: err = %d\n", err);
		DlpCloseDB(pconn, dbh);
		free_pdb(db);
		return -1;
	}
fprintf(stderr, "### Resetting sync flags 3\n");
	err = DlpResetSyncFlags(pconn, dbh);
	if (err != DLPSTAT_NOERR)
	{
		fprintf(stderr, "Can't reset sync flags.\n");
		DlpCloseDB(pconn, dbh);
		free_pdb(db);
		return -1;
	}
}

	/* Clean up */
	free_pdb(db);
	DlpCloseDB(pconn, dbh);

	return 0;		/* Success */
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
