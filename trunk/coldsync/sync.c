/* sync.c
 *
 * Functions for synching a database on the Palm with one one the
 * desktop.
 *
 * $Id: sync.c,v 1.6 1999-03-16 11:56:45 arensb Exp $
 */

#include <stdio.h>
#include <string.h>		/* For memcmp() */
#include "pconn/PConnection.h"
#include "pconn/dlp_cmd.h"
#include "coldsync.h"
#include "pdb.h"
#include "pconn/util.h"		/* For debugging: for debug_dump() */

#define SYNC_DEBUG	1
#ifdef SYNC_DEBUG

int sync_debug = 0;

#define SYNC_TRACE(level, format...) \
	if (sync_debug >= (level)) \
		fprintf(stderr, "SYNC:" format);
#endif	/* SYNC_DEBUG */

static int SyncRecord(struct PConnection *pconn,
		      ubyte dbh,
		      struct pdb *localdb,
		      struct pdb_record *localrec,
		      const struct pdb_record *remoterec);

/* According to the Pigeon Book, here's the logic of syncing:

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
		printf("\tID: 0x%08lx\n", remoterec->id);
		printf("\tattributes: 0x%02x ",
		       remoterec->attributes);
		if (remoterec->attributes & PDB_REC_EXPUNGED)
			printf("EXPUNGED ");
		if (remoterec->attributes & PDB_REC_DIRTY)
			printf("DIRTY ");
		if (remoterec->attributes & PDB_REC_DELETED)
			printf("DELETED ");
		if (remoterec->attributes & PDB_REC_PRIVATE)
			printf("PRIVATE ");
		if (remoterec->attributes & PDB_REC_ARCHIVE)
			printf("ARCHIVE ");
		printf("\n");

		/* Try to find this record in the local database */
		localrec = pdb_FindRecordByID(localdb, remoterec->id);
		if (localrec == NULL)
		{
			/* This record is new. Mark it as modified in the
			 * remote database.
			 */
			fprintf(stderr, "Record 0x%08lx is new.\n",
				remoterec->id);
			remoterec->attributes |= PDB_REC_DIRTY;
			continue;
		}

		/* The record exists. Compare the local and remote versions */

		printf("Local Record:\n");
		printf("\tID: 0x%08lx\n", localrec->id);
		printf("\tattributes: 0x%02x ",
		       localrec->attributes);
		if (localrec->attributes & PDB_REC_EXPUNGED)
			printf("EXPUNGED ");
		if (localrec->attributes & PDB_REC_DIRTY)
			printf("DIRTY ");
		if (localrec->attributes & PDB_REC_DELETED)
			printf("DELETED ");
		if (localrec->attributes & PDB_REC_PRIVATE)
			printf("PRIVATE ");
		if (localrec->attributes & PDB_REC_ARCHIVE)
			printf("ARCHIVE ");
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

	/* For each record in the local database, try to look it up in the
	 * remote database. If the remote record isn't found, the record
	 * has been deleted (and presumably archived elsewhere). Unless the
	 * local record has its dirty flag set, in which case it was added
	 * locally since the last sync.
	 */
fprintf(stderr, "Checking local database entries\n");
	for (localrec = localdb->rec_index.rec;
	     localrec != NULL;
	     localrec = localrec->next)
	{
		/* Try to look this record up in the remote database */
		remoterec = pdb_FindRecordByID(remotedb, localrec->id);
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
			pdb_DeleteRecordByID(localdb, localrec->id);
		}
	}

	/* Phase 2:
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
		printf("\tID: 0x%08lx\n", remoterec->id);
		printf("\tattributes: 0x%02x ",
		       remoterec->attributes);
		if (remoterec->attributes & PDB_REC_EXPUNGED)
			printf("EXPUNGED ");
		if (remoterec->attributes & PDB_REC_DIRTY)
			printf("DIRTY ");
		if (remoterec->attributes & PDB_REC_DELETED)
			printf("DELETED ");
		if (remoterec->attributes & PDB_REC_PRIVATE)
			printf("PRIVATE ");
		if (remoterec->attributes & PDB_REC_ARCHIVE)
			printf("ARCHIVE ");
		printf("\n");
debug_dump(stderr, "REM", remoterec->data, remoterec->data_len);

		/* Find the local version of the record */
		localrec = pdb_FindRecordByID(localdb, remoterec->id);
		if (localrec == NULL)
		{
			struct pdb_record *newrec;

			/* This record is new. Add it to the local database */
fprintf(stderr, "Adding record 0x%08lx to local database\n", remoterec->id);

			/* Fix flags */
			remoterec->attributes &= 0x0f;
				/* XXX - This will just be set to 0 once
				 * pdb_record includes separate fields for
				 * attributes and category.
				 */
				/* XXX - Actually, doesn't the "archived"
				 * flag overlap the high bit of the
				 * category?
				 */
			/* First, make a copy */
			newrec = pdb_CopyRecord(remotedb, remoterec);
			if (newrec == NULL)
			{
				fprintf(stderr, "Can't copy a new record.\n");
				return -1;
			}

			/* Now add the copy to the local database */
			pdb_AppendRecord(localdb, newrec);

			continue;
		}

		printf("Local Record:\n");
		printf("\tID: 0x%08lx\n", localrec->id);
		printf("\tattributes: 0x%02x ",
		       localrec->attributes);
		if (localrec->attributes & PDB_REC_EXPUNGED)
			printf("EXPUNGED ");
		if (localrec->attributes & PDB_REC_DIRTY)
			printf("DIRTY ");
		if (localrec->attributes & PDB_REC_DELETED)
			printf("DELETED ");
		if (localrec->attributes & PDB_REC_PRIVATE)
			printf("PRIVATE ");
		if (localrec->attributes & PDB_REC_ARCHIVE)
			printf("ARCHIVE ");
		printf("\n");

		err = SyncRecord(pconn, dbh, localdb, localrec, remoterec);
fprintf(stderr, "SyncRecord returned %d\n", err);
	}

	/* Write the local database to the backup file */
	err = pdb_Write(localdb, bakfname);
	if (err < 0)
	{
		fprintf(stderr, "SlowSync: error writing local backup.\n");
		return -1;
	}

#if 0
if (remotedbinfo->db_flags & DLPCMD_DBFLAG_OPEN)
{
	fprintf(stderr, "File is open. Not cleaning up, not resetting flags\n");
} else {
#endif	/* 0 */
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
#if 0
}
#endif	/* 0 */

	/* Clean up */
	free_pdb(remotedb);
	DlpCloseDB(pconn, dbh);

	return 0;		/* Success */
}

/* FastSync
 * Do a fast sync of 'localdb' with the Palm. A fast sync is one where this
 * is the last machine that the Palm synced with.
 */
int
FastSync(struct PConnection *pconn,
	 struct dlp_dbinfo *remotedbinfo,
	 struct pdb *localdb,
	 char *bakfname)
{
extern int dlpc_debug;
	int err;

	ubyte dbh;			/* Database handle */
	struct pdb_record *localrec;	/* Record in "local" database */
	struct dlp_recinfo recinfo;	/* Next modified record */
	const ubyte *rptr;		/* Pointer into buffers, for reading */

	SYNC_TRACE(1, "Doing a fast sync of \"%s\" to \"%s\""
		   "(filename \"%s\")\n",
		   remotedbinfo->name,
		   localdb->name, bakfname);

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
		fprintf(stderr, "FastSync: Can't open \"%s\": %d\n",
			remotedbinfo->name, err);
		return -1;
	    default:
		/* Some other error, which probably means the sync can't
		 * continue.
		 */
		fprintf(stderr, "FastSync: Can't open \"%s\": %d\n",
			remotedbinfo->name, err);
		return -1;
	}

	/* Read each modified record in turn, and apply the sync
	 * logic to it.
	 */
dlpc_debug = 10;
	while ((err = DlpReadNextModifiedRec(pconn, dbh,
					     &recinfo, &rptr))
	       == DLPSTAT_NOERR)
	{
		struct pdb_record *remoterec;

fprintf(stderr, "DlpReadNextModifiedRec returned %d\n", err);
		/* Got the next modified record. Deal with it */
		remoterec = new_Record(recinfo.attributes,
				       recinfo.id,
				       recinfo.size,
				       rptr);
		if (remoterec == NULL)
		{
			fprintf(stderr,
				"FastSync: Can't allocate new record\n");
			DlpCloseDB(pconn, dbh);
			return -1;
		}

		/* Look up the modified record in the local database */
		localrec = pdb_FindRecordByID(localdb, remoterec->id);
		if (localrec == NULL)
		{
			/* This record is new. Add it to the local
			 * database.
			 */

			/* Clear the flags in localrec: it's fresh and new. */
			remoterec->attributes &= 0x0f;
				/* XXX - Presumably, this should just
				 * become a zero assignment, when/if
				 * attributes and categories get separated.
				 */

			/* Add the new record to localdb */
			if ((err = pdb_AppendRecord(localdb, remoterec)) < 0)
			{
				fprintf(stderr, "FastSync: Can't append new record to database: %d\n",
					err);
				pdb_FreeRecord(localrec);
				DlpCloseDB(pconn, dbh);
				return -1;
			}

			/* Success. Go on to the next modified record */
			continue;
		}

		/* This record already exists in localdb. */
		err = SyncRecord(pconn, dbh,
				 localdb, localrec,
				 remoterec);
		if (err < 0)
		{
			fprintf(stderr, "FastSync: SyncRecord returned %d\n",
				err);
			pdb_FreeRecord(remoterec);
			DlpCloseDB(pconn, dbh);
			return -1;
		}

		pdb_FreeRecord(remoterec);
				/* We're done with this record */
	}
	switch (err)
	{
	    case DLPSTAT_NOTFOUND:
		/* No more modified records found. Skip to the next part */
fprintf(stderr, "FastSync: no more modified records\n");
		break;
	    default:
		fprintf(stderr, "FastSync: DlpReadNextModifiedRec returned %d\n", err);
		/* XXX */
		DlpCloseDB(pconn, dbh);
		return -1;
		break;
	}

	/* XXX - Go through the local database and cope with new, modified,
	 * and deleted records.
	 */

	/* Write the local database to the backup file */
	err = pdb_Write(localdb, bakfname);
	if (err < 0)
	{
		fprintf(stderr, "SlowSync: error writing local backup.\n");
		DlpCloseDB(pconn, dbh);
		return -1;
	}

#if 0
/* XXX - Why was this added, originally? There was some kind of problem
 * with "Graffiti Shortcuts" being open all the time, and/or ROM databases
 * not being writable.
 */
if (remotedbinfo->db_flags & DLPCMD_DBFLAG_OPEN)
{
	fprintf(stderr, "File is open. Not cleaning up, not resetting flags\n");
} else {
#endif	/* 0 */
fprintf(stderr, "### Cleaning up database\n");
	err = DlpCleanUpDatabase(pconn, dbh);
	if (err != DLPSTAT_NOERR)
	{
		fprintf(stderr, "Can't clean up database: err = %d\n", err);
		DlpCloseDB(pconn, dbh);
/*  		free_pdb(remotedb); */
		return -1;
	}
fprintf(stderr, "### Resetting sync flags 3\n");
	err = DlpResetSyncFlags(pconn, dbh);
	if (err != DLPSTAT_NOERR)
	{
		fprintf(stderr, "Can't reset sync flags.\n");
		DlpCloseDB(pconn, dbh);
/*  		free_pdb(remotedb); */
		return -1;
	}
#if 0
}
#endif	/* 0 */

	/* Clean up */
/*  	free_pdb(remotedb); */
	DlpCloseDB(pconn, dbh);

	return 0;		/* Success */
}

/* pdb_SyncRecord

 * Sync a record.
 */
static int
SyncRecord(struct PConnection *pconn,	/* Connection to Palm */
	   ubyte dbh,			/* Database handle */
	   struct pdb *localdb,		/* Local database */
	   struct pdb_record *localrec,	/* Current record in local database */
	   const struct pdb_record *remoterec)
					/* Current record in remote
					 * database (from Palm).
					 */
			/* XXX - Can remoterec be made const? */
{
	int err;

	/* Figure out what to do with these records. This is long
	 * and hairy, but the basic form is:
	 *	check each possible case for 'remoterec'
	 *	    check each possible case for 'localrec'
	 */
/* Some convenience macros */
#define EXPUNGED(r)	(((r)->attributes & PDB_REC_EXPUNGED) != 0)
#define DIRTY(r)	(((r)->attributes & PDB_REC_DIRTY) != 0)
#define DELETED(r)	(((r)->attributes & PDB_REC_DELETED) != 0)
#define ARCHIVE(r)	(((r)->attributes & PDB_REC_ARCHIVE) != 0)

	/* For each record, there are only four cases to consider:
	 * - DELETED ARCHIVE
	 *	This record has been deleted with archving.
	 * - DELETED EXPUNGE
	 *	This record has been deleted without archiving.
	 * - DIRTY
	 *	This record has been modified.
	 * - nothing
	 *	This record hasn't changed.
	 * All other flag combinations are either absurd or redundant. They
	 * should probably be flagged.
	 */
	if (DELETED(remoterec) && ARCHIVE(remoterec))
	{
		SYNC_TRACE(5, "Remote: deleted, archived\n");
		/* Remote record has been deleted; user wants an
		 * archive copy.
		 */
		if (DELETED(localrec) && ARCHIVE(localrec))
		{
			SYNC_TRACE(5, "Local:  deleted, archived\n");
			/* Local record has been deleted; user wants an
			 * archive copy.
			 */

			/* XXX - If the contents are identical, archive one
			 * copy. Otherwise, archive both copies.
			 */

			/* Delete the record on the Palm */
			SYNC_TRACE(6, "> Deleting record on Palm\n");
			err = DlpDeleteRecord(pconn, dbh, 0,
					      remoterec->id);
			if (err != DLPSTAT_NOERR)
			{
				fprintf(stderr, "SlowSync: Warning: can't delete record 0x%08lx: %d\n",
					remoterec->id, err);
				/* XXX - For now, just ignore this,
				 * since it's probably not a show
				 * stopper.
				 */
			}

			/* Delete localrec */
			SYNC_TRACE(6, "> Deleting record in local database\n");
			pdb_DeleteRecordByID(localdb, localrec->id);

		} else if (DELETED(localrec) && EXPUNGED(localrec))
		{
			/* Local record has been deleted without a trace. */
			SYNC_TRACE(5, "Local:  deleted, expunged\n");

			/* XXX - Archive remoterec */
			SYNC_TRACE(6, "> Need to archive remote record\n");

			/* Delete the record on the Palm */
			SYNC_TRACE(6, "> Deleting record on Palm\n");
			err = DlpDeleteRecord(pconn, dbh, 0,
					      remoterec->id);
			if (err != DLPSTAT_NOERR)
			{
				fprintf(stderr, "SlowSync: Warning: can't delete record 0x%08lx: %d\n",
					remoterec->id, err);
				/* XXX - For now, just ignore this,
				 * since it's probably not a show
				 * stopper.
				 */
			}

			/* Delete localrec */
			SYNC_TRACE(6, "> Deleting record in local database\n");
			pdb_DeleteRecordByID(localdb, localrec->id);

		} else if (DIRTY(localrec))
		{
			udword newID;	/* ID of uploaded record */

			/* Local record has changed */
			SYNC_TRACE(5, "Local:  dirty\n");

			/* XXX - Archive remoterec */
			SYNC_TRACE(6, "> Need to archive remote record\n");

			/* Fix flags */
			localrec->attributes &= 0x0f;
				/* XXX - This will just be set to 0, once
				 * pdb_record has separate fields for
				 * attributes and category.
				 */

			/* Upload localrec to Palm */
			SYNC_TRACE(6, "> Sending local record to Palm\n");
			err = DlpWriteRecord(pconn, dbh, 0x80,
					     localrec->id,
					     /* XXX - The category is the
					      * bottom 4 bits of the
					      * attributes. Fix this
					      * throughout.
					      */
					     localrec->attributes & 0xf0,
					     localrec->attributes & 0x0f,
					     localrec->data_len,
					     localrec->data,
					     &newID);
			if (err != DLPSTAT_NOERR)
			{
				fprintf(stderr,
					"Error uploading record 0x%08lx: %d\n",
					localrec->id, err);
				return -1;
			}

			/* The record was assigned a (possibly new) unique
			 * ID when it was uploaded. Make sure the local
			 * database reflects this.
			 */
			localrec->id = newID;
			SYNC_TRACE(7, "newID == 0x%08lx\n", newID);

		} else {
			/* Local record hasn't changed */
			SYNC_TRACE(5, "Local:  clean\n");

			/* XXX - Archive remoterec */
			SYNC_TRACE(6, "> Need to archive remote record\n");

			/* Delete localrec */
			SYNC_TRACE(6, "> Deleting record in local database\n");
			pdb_DeleteRecordByID(localdb, localrec->id);

		}
	} else if (DELETED(remoterec) && EXPUNGED(remoterec))
	{
		/* Remote record has been deleted without a trace. */
		SYNC_TRACE(5, "Remote: deleted, expunged\n");

		if (DELETED(localrec) && ARCHIVE(localrec))
		{
			/* Local record has been deleted; user wants an
			 * archive copy.
			 */
			SYNC_TRACE(5, "Local:  deleted, archived\n");

			/* XXX - Fix flags */
			/* XXX - Archive localrec */
			SYNC_TRACE(6, "> Need to archive local record\n");

			/* Delete localrec */
			SYNC_TRACE(6, "> Deleting record in local database\n");
			pdb_DeleteRecordByID(localdb, localrec->id);

		} else if (DELETED(localrec) && EXPUNGED(localrec))
		{
			/* Local record has been deleted without a trace. */
			SYNC_TRACE(5, "Local:  deleted, expunged\n");

			/* Delete localrec */
			SYNC_TRACE(6, "> Deleting record in local database\n");
			pdb_DeleteRecordByID(localdb, localrec->id);

		} else if (DIRTY(localrec))
		{
			/* Local record has changed */

			udword newID;	/* ID of uploaded record */

			SYNC_TRACE(5, "Local:  dirty\n");

			/* Delete remoterec */
			SYNC_TRACE(6, "> Deleting remote record\n");
			/* XXX - DlpDeleteRecord(remoterec->id) */

			/* Fix flags */
			localrec->attributes &= 0x0f;
				/* XXX - This will just be 0 once
				 * pdb_record includes separate fields for
				 * attributes and category.
				 */

			/* Upload localrec to Palm */
			SYNC_TRACE(6, "> Uploading local record to Palm\n");
			err = DlpWriteRecord(pconn, dbh, 0x80,
					     localrec->id,
					     /* XXX - The bottom nybble of
					      * the attributes byte is the
					      * category. Fix this
					      * throughout.
					      */
					     localrec->attributes & 0xf0,
					     localrec->attributes & 0xf0,
					     localrec->data_len,
					     localrec->data,
					     &newID);
			if (err != DLPSTAT_NOERR)
			{
				fprintf(stderr,
					"Error uploading record 0x%08lx: %d\n",
					localrec->id,
					err);
				return -1;
			}

			/* The record was assigned a (possibly new) unique
			 * ID when it was uploaded. Make sure the local
			 * database reflects this.
			 */
			localrec->id = newID;
			SYNC_TRACE(7, "newID == 0x%08lx\n", newID);

		} else {
			/* Local record hasn't changed */
			SYNC_TRACE(5, "Local:  clean\n");

			/* Delete localrec */
			SYNC_TRACE(6, "> Deleting record in local database\n");
			pdb_DeleteRecordByID(localdb, localrec->id);

		}
	} else if (DIRTY(remoterec))
	{
		/* Remote record has changed */
		SYNC_TRACE(5, "Remote: dirty\n");

		/* XXX - Can these next two cases be combined? If the local
		 * record has been deleted, it needs to be deleted. The
		 * only difference is that if it's been archived, need to
		 * archive it first.
		 * XXX - In fact, can this be done throughout?
		 */
		if (DELETED(localrec) && ARCHIVE(localrec))
		{
			/* Local record has been deleted; user wants an
			 * archive copy.
			 */
			struct pdb_record *newrec;

			SYNC_TRACE(5, "Local:  deleted, archived\n");

			/* Fix flags */
			localrec->attributes &= 0x0f;
				/* XXX - This will just be 0 once
				 * pdb_record includes separate fields for
				 * attributes and category.
				 */

			/* XXX - Archive localrec */
			SYNC_TRACE(6, "> Need to archive local record\n");

			/* Copy remoterec to localdb */
			SYNC_TRACE(6, "> Copying remote record to local database\n");
			newrec = new_Record(remoterec->attributes,
					    remoterec->id,
					    remoterec->data_len,
					    remoterec->data);
			if (newrec == NULL)
			{
				fprintf(stderr, "SyncRecord: can't copy record\n");
				return -1;
			}

			/* Fix flags */
			localrec->attributes &= 0x0f;
				/* XXX - This will just be 0 once
				 * pdb_record includes separate fields for
				 * attributes and category.
				 */

			pdb_AppendRecord(localdb, newrec);

		} else if (DELETED(localrec) && EXPUNGED(localrec))
		{
			/* Local record has been deleted without a trace. */
			struct pdb_record *newrec;

			SYNC_TRACE(5, "Local:  deleted, expunged\n");

			/* Delete localrec */
			SYNC_TRACE(6, "> Deleting local record\n");
			pdb_DeleteRecordByID(localdb, localrec->id);

			/* Copy remoterec to localdb */
			SYNC_TRACE(6, "> Copying remote record to local database\n");
			newrec = new_Record(remoterec->attributes,
					    remoterec->id,
					    remoterec->data_len,
					    remoterec->data);
			if (newrec == NULL)
			{
				fprintf(stderr, "SyncRecord: can't copy record\n");
				return -1;
			}

			/* Fix remote flags */
			newrec->attributes &= 0x0f;
				/* XXX - This will just be 0 once
				 * pdb_record includes separate fields for
				 * attributes and category.
				 */

			pdb_AppendRecord(localdb, newrec);

		} else if (DIRTY(localrec))
		{
			/* Local record has changed */
			SYNC_TRACE(5, "Local:  dirty\n");

			/* XXX - If the records are identical, do nothing
			 * (unset dirty flags). Otherwise, copy remoterec
			 * to localdb and copy localrec to remotedb.
			 */

			/* XXX - Fix flags */
			SYNC_TRACE(6, "> I should do something here\n");
		} else {
			/* Local record has not changed */
			struct pdb_record *newrec;

			SYNC_TRACE(5, "Local:  clean\n");

			SYNC_TRACE(6, "> Replacing local record with remote one\n");
			/* Replace localrec with remoterec.
			 * This is done in three stages:
			 * - copy 'remoterec' to 'newrec'
			 * - insert 'remoterec' after 'localrec'
			 * - delete 'localrec'
			 */
			SYNC_TRACE(6, "> Copying remote record to local database\n");
			newrec = new_Record(remoterec->attributes,
					    remoterec->id,
					    remoterec->data_len,
					    remoterec->data);
			if (newrec == NULL)
			{
				fprintf(stderr, "SyncRecord: can't copy record\n");
				return -1;
			}

			/* Fix flags */
			newrec->attributes &= 0x0f;
				/* XXX - This will just be 0 once
				 * pdb_record includes separate fields for
				 * attributes and category.
				 */

			err = pdb_InsertRecord(localdb, localrec, newrec);
			if (err < 0)
			{
				fprintf(stderr, "SlowSync: Can't insert record 0x%08lx\n",
					newrec->id);
				pdb_FreeRecord(newrec);
				return -1;
			}

			pdb_DeleteRecordByID(localdb, localrec->id);
				/* We don't check for errors here, because,
				 * well, we wanted to delete it anyway.
				 */
		}
	} else {
		/* Remote record has not changed */
		SYNC_TRACE(5, "Remote: clean\n");

		if (DELETED(localrec) && ARCHIVE(localrec))
		{
			/* Local record has been deleted; user wants an
			 * archive copy.
			 */
			SYNC_TRACE(5, "Local:  deleted, archived\n");

			/* Fix local flags */
			localrec->attributes &= 0x0f;
				/* XXX - This will just be 0 once
				 * pdb_record includes separate fields for
				 * attributes and category.
				 */

			/* XXX - Archive localrec */
			SYNC_TRACE(6, "> Need to archive local record\n");

			/* Delete localrec */
			SYNC_TRACE(6, "> Deleting record in local database\n");
			pdb_DeleteRecordByID(localdb, localrec->id);

		} else if (DELETED(localrec) && EXPUNGED(localrec))
		{
			/* Local record has been deleted without a trace. */
			SYNC_TRACE(5, "Local:  deleted, expunged\n");

			/* Delete localrec */
			SYNC_TRACE(6, "> Deleting record in local database\n");
			pdb_DeleteRecordByID(localdb, localrec->id);

		} else if (DIRTY(localrec))
		{
			/* Local record has changed */
			udword newID;	/* ID of uploaded record */

			SYNC_TRACE(5, "Local:  dirty\n");

			/* Fix local flags */
			localrec->attributes &= 0x0f;
				/* XXX - This will just be 0 once
				 * pdb_record includes separate fields for
				 * attributes and category.
				 */

			/* Upload localrec to Palm */
			SYNC_TRACE(6, "> Uploading local record to Palm\n");
			err = DlpWriteRecord(pconn, dbh, 0x80,
					     localrec->id,
					     /* XXX - The bottom nybble of
					      * the attributes byte is the
					      * category. Fix this
					      * throughout.
					      */
					     localrec->attributes & 0xf0,
					     localrec->attributes & 0xf0,
					     localrec->data_len,
					     localrec->data,
					     &newID);
			if (err != DLPSTAT_NOERR)
			{
				fprintf(stderr,
					"Error uploading record 0x%08lx: %d\n",
					localrec->id,
					err);
				return -1;
			}

			/* The record was assigned a (possibly new) unique
			 * ID when it was uploaded. Make sure the local
			 * database reflects this.
			 */
			localrec->id = newID;
			SYNC_TRACE(7, "newID == 0x%08lx\n", newID);

		} else {
			/* Local record hasn't changed */
			SYNC_TRACE(5, "Local:  clean\n");

			/* Do nothing */
			SYNC_TRACE(6, "> Not doing anything\n");
		}
	}

#undef EXPUNGED
#undef DIRTY
#undef DELETED
#undef ARCHIVE

	return 0;	/* Success */
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
