/* sync.c
 *
 * Functions for synching a database on the Palm with one one the
 * desktop.
 *
 * $Id: sync.c,v 1.4 1999-03-11 10:04:42 arensb Exp $
 */

#include <stdio.h>
#include "pconn/PConnection.h"
#include "pconn/dlp_cmd.h"
#include "coldsync.h"
#include "pdb.h"
#include "pconn/util.h"		/* For debugging: for debug_dump() */

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
	remote record has been delete. Delete it. Mark it as deleted (in
	the remote database?) (If it was supposed to be archived,
	presumably it got archived somewhere else.)
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

int
SlowSync(struct PConnection *pconn,
	 struct dlp_dbinfo *remotedbinfo,
	 struct pdb *localdb,
	 char *bakfname)
{
	int err;
	struct pdb *remotedb;	/* The database on the Palm will be read
				 * into here */
	ubyte dbh;		/* Database handle */
	struct pdb_record *remoterec;	/* Record in "remote" database */
	struct pdb_record *localrec;	/* Record in "local" database */

fprintf(stderr, "Doing a slow sync of \"%s\" to \"%s\" (filename \"%s\")\n",
	remotedbinfo->name, localdb->name, bakfname);

	/* Tell the Palm we're synchronizing a database */
	err = DlpOpenConduit(pconn);
	if (err != DLPSTAT_NOERR)
		return -1;

	/* Open the database */
	/* XXX - Card #0 shouldn't be hardwired. It probably ought to be a
	 * field in dlp_dbinfo or something.
	 */
	if (remotedbinfo->db_flags & DLPCMD_DBFLAG_OPEN)
		fprintf(stderr, "This database is open. Not opening for writing\n");
	err = DlpOpenDB(pconn, 0, remotedbinfo->name,
			DLPCMD_MODE_READ |
			(remotedbinfo->db_flags & DLPCMD_DBFLAG_OPEN ?
			 0 :
			 DLPCMD_MODE_WRITE) |
			DLPCMD_MODE_SECRET,
				/* "Secret" records aren't actually secret.
				 * They're actually just the ones marked
				 * private, and they're not at all secret.
				 */
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
		fprintf(stderr, "SlowSync: Can't open \"%s\": %d\n",
			remotedbinfo->name, err);
		return -1;
	    default:
		/* Some other error, which probably means the sync can't
		 * continue.
		 */
		fprintf(stderr, "SlowSync: Can't open \"%s\": %d\n",
			remotedbinfo->name, err);
		return -1;
	}

	/* Phase 1:
	 * Read the entire database from the Palm to an in-memory copy. Go
	 * through each record one at a time, and see what has changed
	 * since the last time this database was synced with this machine.
	 * Only then will we be able to do the actual sync.
	 */

	/* Read the database on the Palm to an in-memory copy */
	if ((remotedb = pdb_Download(pconn, remotedbinfo, dbh)) == NULL)
	{
		fprintf(stderr, "Can't downlod \"%s\"\n",
			remotedbinfo->name);
		DlpCloseDB(pconn, dbh);
		return -1;
	}

	/* Look through each record in the remote database, comparing it to
	 * the copy in the local database.
	 */
	/* XXX - This ought to handle both record and resource databases */
fprintf(stderr, "*** Phase 1:\n");
	for (remoterec = remotedb->rec_index.rec;
	     remoterec != NULL;
	     remoterec = remoterec->next)
	{
		printf("Remote Record:\n");
		printf("\tuniqueID: 0x%08lx\n", remoterec->uniqueID);
		printf("\tattributes: 0x%02x ",
		       remoterec->attributes);
		if (remoterec->attributes & PDB_REC_DELETED)
			printf("DELETED ");
		if (remoterec->attributes & PDB_REC_DIRTY)
			printf("DIRTY ");
		if (remoterec->attributes & PDB_REC_BUSY)
			printf("BUSY ");
		if (remoterec->attributes & PDB_REC_PRIVATE)
			printf("PRIVATE ");
		if (remoterec->attributes & PDB_REC_ARCHIVED)
			printf("ARCHIVED ");
		printf("\n");

		/* Try to find this record in the local database */
		localrec = pdb_FindRecordByID(localdb, remoterec->uniqueID);
		if (localrec == NULL)
		{
			/* This record is new. Mark it as modified in the
			 * remote database.
			 */
			fprintf(stderr, "Record 0x%08lx is new.\n",
				remoterec->uniqueID);
			remoterec->attributes |= PDB_REC_DIRTY;
			continue;
		}

		/* The record exists. Compare the local and remote versions */

		printf("Local Record:\n");
		printf("\tuniqueID: 0x%08lx\n", localrec->uniqueID);
		printf("\tattributes: 0x%02x ",
		       localrec->attributes);
		if (localrec->attributes & PDB_REC_DELETED)
			printf("DELETED ");
		if (localrec->attributes & PDB_REC_DIRTY)
			printf("DIRTY ");
		if (localrec->attributes & PDB_REC_BUSY)
			printf("BUSY ");
		if (localrec->attributes & PDB_REC_PRIVATE)
			printf("PRIVATE ");
		if (localrec->attributes & PDB_REC_ARCHIVED)
			printf("ARCHIVED ");
		printf("\n");

		/* Compare the records' length. If they differ, then the
		 * records differ.
		 */
		if (remoterec->data_len != localrec->data_len)
		{
			/* The sizes differ; they're different. Mark the
			 * record in the remote database as dirty: it
			 * should be added as a separate record.
			 */
			fprintf(stderr, "The size is different. This record has been modified.\n");
			remoterec->attributes |= PDB_REC_DIRTY;
			continue;
		}

		/* The lengths are the same. Have to do a byte-by-byte
		 * comparison.
		 */
		if (memcmp(remoterec->data, localrec->data,
			   remoterec->data_len) != 0)
		{
			/* They're different. Mark the record in the remote
			 * database as dirty: it should be added as a
			 * separate record.
			 */
			fprintf(stderr, "The records are different.\n");
			remoterec->attributes |= PDB_REC_DIRTY;
			continue;
		}

fprintf(stderr, "The records are identical.\n");
		/* The two records are identical. Clear the dirty flags */
		remoterec->attributes &= ~PDB_REC_DIRTY;
		localrec->attributes &= ~PDB_REC_DIRTY;
	}

	/* XXX - For each record in the local database, try to look it up
	 * in the remote database. If not found, the record has been
	 * deleted (and presumably archived elsewhere).
	 */
	/* XXX - Actually, if the local record was added after the last
	 * sync time (as reported by the Palm, and correcting for time zone
	 * differences), then the local record was added locally, and
	 * should be added to the Palm. Or should the local copy have its
	 * dirty bit set?
	 */
fprintf(stderr, "Checking local database entries\n");
	for (localrec = localdb->rec_index.rec;
	     localrec != NULL;
	     localrec = localrec->next)
	{
		/* Try to look this record up in the remote database */
		remoterec = pdb_FindRecordByID(remotedb, localrec->uniqueID);
		if (remoterec != NULL)
			/* The record exists in the remote database. That
			 * means we've dealt with it in the previous
			 * section, so we can skip it now.
			 */
			continue;

		/* The local record doesn't exist in the remote database.
		 * If its dirty flag is set, that means it was added
		 * locally since the last sync, and should be added to the
		 * remote database. If the local record isn't dirty, that
		 * means it was deleted on the Palm (and, presumably,
		 * archived someplace else if it needed to be archived);
		 * delete it from the local database.
		 */
		if ((localrec->attributes & PDB_REC_DIRTY) == 0)
		{
			/* Delete this record from the local database;
			 * presumably it was archived elsewhere.
			 */
fprintf(stderr, "Deleting this record: it's clean locally but doesn't exist in the remote database\n");
			pdb_DeleteRecordByID(localdb, localrec->uniqueID);
		}
	}

	/* XXX - Phase 2:
	 * Now that each record in the remote database has been examined,
	 * and it has been decided whether it is new, deleted, etc., go
	 * through the database once again and do a proper sync, following
	 * the same logic as for fast syncs.
	 */
	/* XXX - It may be possible to fold phases 1 and 2 into one. But is
	 * this desirable?
	 */
fprintf(stderr, "*** Phase 2:\n");
	for (remoterec = remotedb->rec_index.rec;
	     remoterec != NULL;
	     remoterec = remoterec->next)
	{
		printf("Remote Record:\n");
		printf("\tuniqueID: 0x%08lx\n", remoterec->uniqueID);
		printf("\tattributes: 0x%02x ",
		       remoterec->attributes);
		if (remoterec->attributes & PDB_REC_DELETED)
			printf("DELETED ");
		if (remoterec->attributes & PDB_REC_DIRTY)
			printf("DIRTY ");
		if (remoterec->attributes & PDB_REC_BUSY)
			printf("BUSY ");
		if (remoterec->attributes & PDB_REC_PRIVATE)
			printf("PRIVATE ");
		if (remoterec->attributes & PDB_REC_ARCHIVED)
			printf("ARCHIVED ");
		printf("\n");
debug_dump(stderr, "REM", remoterec->data, remoterec->data_len);
	}

if (remotedbinfo->db_flags & DLPCMD_DBFLAG_OPEN)
{
	fprintf(stderr, "File is open. Not cleaning up, not resetting flags\n");
} else {
fprintf(stderr, "### Cleaning up database\n");
	err = DlpCleanUpDatabase(pconn, dbh);
	if (err != DLPSTAT_NOERR)
	{
		fprintf(stderr, "Can't clean up database: err = %d\n", err);
		DlpCloseDB(pconn, dbh);
		free_pdb(remotedb);
		return -1;
	}
fprintf(stderr, "### Resetting sync flags 3\n");
	err = DlpResetSyncFlags(pconn, dbh);
	if (err != DLPSTAT_NOERR)
	{
		fprintf(stderr, "Can't reset sync flags.\n");
		DlpCloseDB(pconn, dbh);
		free_pdb(remotedb);
		return -1;
	}
}

	/* Clean up */
	free_pdb(remotedb);
	DlpCloseDB(pconn, dbh);

	return 0;		/* Success */
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
