/* GenericConduit.cc
 *
 * Methods and such for the generic conduit.
 *
 * $Id: GenericConduit.cc,v 1.3 1999-08-01 07:59:41 arensb Exp $
 */
#include "config.h"
#include <iostream.h>
#include <iomanip.h>		// Probably only needed for debugging
#include <stdio.h>		// For perror(), rename()
#include <stdlib.h>		// For free()
#include <fcntl.h>		// For open()
#include <sys/stat.h>
#include <sys/param.h>		// For MAXPATHLEN
#include <string.h>
#include <errno.h>
#include "GenericConduit.hh"

extern "C" {
/* XXX - Should all of the "standard" header files be inside this block? */
#include <unistd.h>
#include "dlp_cmd.h"
#include "util.h"
#include "coldsync.h"
#include "archive.h"
#include "pdb.h"

extern int add_to_log(char *msg);
}

/* Convenience functions */
static inline bool EXPUNGED(const struct pdb_record *r)
{ return (r->attributes & PDB_REC_EXPUNGED) != 0; }

static inline bool DIRTY(const struct pdb_record *r)
{ return (r->attributes & PDB_REC_DIRTY) != 0; }

static inline bool DELETED(const struct pdb_record *r)
{ return (r->attributes & PDB_REC_DELETED) != 0; }

static inline bool ARCHIVE(const struct pdb_record *r)
{ return (r->attributes & PDB_REC_ARCHIVE) != 0; }

static inline bool PRIVATE(const struct pdb_record *r)
{ return (r->attributes & PDB_REC_PRIVATE) != 0; }

int
run_GenericConduit(
	struct PConnection *pconn,
	struct Palm *palm,
	struct dlp_dbinfo *dbinfo)
{
	GenericConduit gc(pconn, palm, dbinfo);

	return gc.run();
}

/* GenericConduit::GenericConduit
 * Constructor
 */
GenericConduit::GenericConduit(
	struct PConnection *pconn,
	struct Palm *palm,
	struct dlp_dbinfo *dbinfo) :
	_pconn(pconn),
	_palm(palm),
	_dbinfo(dbinfo),
	_localdb(0),
	_remotedb(0),
	_archfd(-1)		// No archive file yet
{}

/* GenericConduit::~GenericConduit
 * Destructor.
 */
GenericConduit::~GenericConduit()
{
	/* _pconn, _palm and _dbinfo are inherited from the function
	 * that called the constructor, so don't free them.
	 */

	if (_localdb != 0)
		free_pdb(_localdb);
	if (_remotedb != 0)
		free_pdb(_remotedb);
}

/* XXX - If syncing a ROM database, bear in mind that you can't upload
 * records.
 */
/* GenericConduit::run
 * Do the sync. Actually, all this method does is to figure out which
 * method needs to do the syncing, and calls it.
 * If the backup file doesn't exist, then this is evidently the first
 * time that this database has been synced, so run FirstSync().
 * Otherwise, call either FastSync or SlowSync(), depending on whether
 * the last sync was with this machine or some other one.
 */
int
GenericConduit::run()
{
	int err;

	/* See if it's a ROM database. If so, just ignore it, since it
	 * can't be modified and hence need not be synced.
	 */
	if (DBINFO_ISROM(_dbinfo) && !global_opts.check_ROM)
	{
		SYNC_TRACE(3)
			cerr << "\"" << _dbinfo->name
			     << "\" is a ROM database. Ignoring it." << endl;
		add_to_log("ROM\n"); 
		return 0; 
	}
	SYNC_TRACE(4)
		cerr << "\"" << _dbinfo->name
		     << "\" is not a ROM database, or else "
		        "I'm not ignoring it." << endl;

	/* Resource databases are entirely different beasts, so don't
	 * sync them. This function should never be called for a
	 * resource database, though, so this test falls into the
	 * "this should never happen" category.
	 */
	if (DBINFO_ISRSRC(_dbinfo))
	{
		cerr << "I don't deal with resource databases." << endl;
		add_to_log("Not synced\n");
		return -1;
	}

	/* Read the backup file, and put the local database in _localdb */
	err = this->read_backup();
	if (err < 0)
	{
		cerr << "GenericConduit error: Can't read "
		     << _dbinfo->name
		     << " backup file." << endl;
		return -1;
	}

	this->open_archive();		// Initialize the archive file

	if (_localdb == 0)
	{
		/* Failed to read the backup file, but it wasn't an error.
		 * Hence, it's because the backup file doesn't exist, so do
		 * a FirstSync().
		 */
		err = this->FirstSync();
	} else if (global_opts.force_slow || need_slow_sync)
	{
		SYNC_TRACE(3)
			cerr << "Doing a slow sync." << endl;
		err = this->SlowSync();
	} else {
		SYNC_TRACE(3)
			cerr << "Doing a fast sync." << endl;
		err = this->FastSync(); 
	}

	this->close_archive();		// Close the archive file
	return err;
}

/* GenericConduit::FirstSync
 * This function gets called the first time a database is synced. It
 * is responsible for creating the backup file (and any other
 * initialization that a conduit might wish to do).
 * For the generic conduit, just download the entire database, archive
 * any records that need to be archived, and write everything else to
 * the backup file.
 */
int
GenericConduit::FirstSync()
{
	int err;

	/* Tell the Palm we're beginning a new sync. */
	ubyte dbh;		// Database handle

	add_to_log(_dbinfo->name);
	add_to_log(" (1st) - ");

	err = DlpOpenConduit(_pconn);
	if (err != DLPSTAT_NOERR)
	{
		SYNC_TRACE(4)
			cerr << "DlpOpenConduit() returned " << err << endl;
		add_to_log("Error\n");
		return -1; 
	}

	/* Open the database for reading */
	err = DlpOpenDB(_pconn,
			CARD0,
			_dbinfo->name,
			DLPCMD_MODE_READ |
			(_dbinfo->db_flags & DLPCMD_DBFLAG_OPEN ?
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
		/* Can't complete this particular operation, but
		 * it's not a show-stopper. The sync can go on.
		 */
		cerr << "GenericConduit::FirstSync: Can't open \""
		     << _dbinfo->name << "\": "
		     << err << endl;
		add_to_log("Error\n");
		return -1;
	    default:
		/* Some other error, which probably means the sync
		 * can't continue.
		 */
		cerr << "GenericConduit::FirstSync: Can't open \""
		     << _dbinfo->name << "\": "
		     << err << endl;
		add_to_log("Error\n");
		return -1;
	}

	/* Download the database from the Palm to _remotedb */
	_remotedb = pdb_Download(_pconn, _dbinfo, dbh);
	if (_remotedb == 0)
	{
		cerr << "pdb_Download() failed." << endl;
		err = DlpCloseDB(_pconn, dbh);	// Close the database
		add_to_log("Error\n");
		return -1;
	}

	/* Go through each record and clean it up */
	struct pdb_record *remoterec;	// Record in remote database

	for (remoterec = _remotedb->rec_index.rec;
	     remoterec != 0;
	     remoterec = remoterec->next)
	{
		SYNC_TRACE(5)
		{
			cerr << "Remote Record:" << endl;
			cerr << "\tID: 0x"
			     << hex << setw(8) << setfill('0')
			     << remoterec->id << endl;
			cerr << "\tattributes: 0x" << hex << setw(2)
			     << setfill('0')
			     << static_cast<int>(remoterec->attributes)
			     << " ";
			if (EXPUNGED(remoterec))	cerr << "EXPUNGED ";
			if (DIRTY(remoterec))		cerr << "DIRTY ";
			if (DELETED(remoterec))		cerr << "DELETED ";
			if (PRIVATE(remoterec))		cerr << "PRIVATE ";
			if (ARCHIVE(remoterec))		cerr << "ARCHIVE ";
			cerr << endl;
		}

		/* This hairy expression just boils down to "if
		 * remoterec is DELETED and ARCHIVED, or at least
		 * DELETED and not EXPUNGED". That's because some
		 * applications aren't polite enough to set the
		 * ARCHIVED flag when they delete a record.
		 */
		if (DELETED(remoterec) &&
		    ((ARCHIVE(remoterec) ||
		     (!EXPUNGED(remoterec)))))
		{
			// Clear flags
			remoterec->attributes &=
				~(PDB_REC_EXPUNGED |
				  PDB_REC_DIRTY |
				  PDB_REC_DELETED |
				  PDB_REC_ARCHIVE);

			// Archive this record
			SYNC_TRACE(5)
				cerr << "Archiving this record" << endl;
			this->archive_record(remoterec);
		} else if (EXPUNGED(remoterec))
		{
			// Delete this record
			SYNC_TRACE(5)
				cerr << "Deleting this record" << endl; 
			pdb_DeleteRecordByID(_remotedb,
					     remoterec->id);
		} else {
			SYNC_TRACE(5)
				cerr << "Need to save this record" << endl; 
			// Clear flags
			remoterec->attributes &=
				~(PDB_REC_EXPUNGED |
				  PDB_REC_DIRTY |
				  PDB_REC_DELETED |
				  PDB_REC_ARCHIVE);
		}
	}

	// Write the database to its backup file
	err = this->write_backup(_remotedb);
	if (err < 0)
	{
		cerr << "GenericConduit error: Can't write backup file."
		     << endl;
		err = DlpCloseDB(_pconn, dbh);	// Close the database
		add_to_log("Error\n");
		return -1;
	}

	/* Post-sync cleanup */
	if (!IS_RSRC_DB(_remotedb))
	{
		/* Since this function doesn't deal with resource
		 * databases, the test above really just serves to
		 * catch impossible errors.
		 */
		SYNC_TRACE(3)
			cerr << "### Cleaning up database." << endl;
		err = DlpCleanUpDatabase(_pconn, dbh);
		if (err != DLPSTAT_NOERR)
		{
			cerr << "GenericConduit error: Can't clean up database: "
			     << err << endl;
			err = DlpCloseDB(_pconn, dbh);
			add_to_log("Error\n");
			return -1;
		}
	}

	SYNC_TRACE(3)
		cerr << "### Resetting sync flags." << endl;
	err = DlpResetSyncFlags(_pconn, dbh);
	if (err != DLPSTAT_NOERR)
	{
		cerr << "GenericConduit error: Can't reset sync flags: "
		     << err << endl;
		err = DlpCloseDB(_pconn, dbh);
		add_to_log("Error\n");
		return -1;
	}

	/* Clean up */
	err = DlpCloseDB(_pconn, dbh);	// Close the database

	add_to_log("OK\n");
	return 0; 
}

/* GenericConduit::SlowSync
 * A slow sync is done when the last machine that the Palm synced with
 * was not this one. As a result, we can't trust the records' flags,
 * and must download the entire database and compare it with the local
 * copy.
 */
int
GenericConduit::SlowSync()
{
	int err;
	ubyte dbh;			// Database handle
	struct pdb_record *localrec;	// Record in local database
	struct pdb_record *remoterec;	// Record in remote database

	add_to_log(_dbinfo->name);
	add_to_log(" - ");

	SYNC_TRACE(3)
		cerr << "*** Phase 1:" << endl;
	/* Phase 1: grab the entire database from the Palm */
	err = DlpOpenConduit(_pconn);
	if (err != DLPSTAT_NOERR)
	{
		SYNC_TRACE(4)
			cerr << "DlpOpenConduit() returned " << err << endl;
		add_to_log("Error\n");
		return -1; 
	}

	/* Open the database for reading */
	err = DlpOpenDB(_pconn,
			CARD0,
			_dbinfo->name,
			DLPCMD_MODE_READ |
			(_dbinfo->db_flags & DLPCMD_DBFLAG_OPEN ?
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
		/* Can't complete this particular operation, but
		 * it's not a show-stopper. The sync can go on.
		 * XXX - Need to indicate to the caller that the sync
		 * can go on.
		 */
		cerr << "GenericConduit::SlowSync: Can't open \""
		     << _dbinfo->name << "\": "
		     << err << endl;
		add_to_log("Error\n");
		return -1;
	    default:
		/* Some other error, which probably means the sync
		 * can't continue.
		 * XXX - Need to indicate this to the caller.
		 */
		cerr << "GenericConduit::SlowSync: Can't open \""
		     << _dbinfo->name << "\": "
		     << err << endl;
		add_to_log("Error\n");
		return -1;
	}

	/* Download the entire remote database */
	_remotedb = pdb_Download(_pconn, _dbinfo, dbh);
	if (_remotedb == 0)
	{
		cerr << "GenericConduit error: Can't download \""
		     << _dbinfo->name << '"' << endl;
		DlpCloseDB(_pconn, dbh);
		add_to_log("Error\n");
		return -1;
	}

	/* Check each remote record in turn, and compare it to the
	 * copy in the local database.
	 */
	SYNC_TRACE(3)
		cerr << "Checking remote database entries." << endl;
	for (remoterec = _remotedb->rec_index.rec;
	     remoterec != 0;
	     remoterec = remoterec->next)
	{
		SYNC_TRACE(5)
		{
			cerr << "Remote Record:" << endl;
			cerr << "\tID: 0x"
			     << hex << setw(8) << setfill('0')
			     << remoterec->id << endl;
			cerr << "\tattributes: 0x" << hex << setw(2)
			     << setfill('0')
			     << static_cast<int>(remoterec->attributes)
			     << " ";
			if (EXPUNGED(remoterec))	cerr << "EXPUNGED ";
			if (DIRTY(remoterec))		cerr << "DIRTY ";
			if (DELETED(remoterec))		cerr << "DELETED ";
			if (PRIVATE(remoterec))		cerr << "PRIVATE ";
			if (ARCHIVE(remoterec))		cerr << "ARCHIVE ";
			cerr << endl;
			debug_dump(stderr, "REM", remoterec->data,
				   remoterec->data_len > 64 ?
					64 : remoterec->data_len);
		}

		/* Look the record up in the local database. */
		localrec = pdb_FindRecordByID(_localdb, remoterec->id);
		if (localrec == 0)
		{
			/* This remote record doesn't exist in the local
			 * database. It has evidently been added since the
			 * last sync with this machine, but it may also
			 * have been deleted since it was added.
			 */

			if (DELETED(remoterec) &&
			    (ARCHIVE(remoterec) ||
			     !EXPUNGED(remoterec)))
			{
				/* This record was deleted. Either it was
				 * explicitly marked as archived, or at
				 * least not explicitly marked as expunged.
				 * So archive it.
				 */
				remoterec->attributes &=
					~(PDB_REC_EXPUNGED |
					  PDB_REC_DIRTY |
					  PDB_REC_DELETED |
					  PDB_REC_ARCHIVE);

				// Archive this record
				SYNC_TRACE(5)
					cerr << "Archiving this record"
					     << endl;
				this->archive_record(remoterec);
			} else if (EXPUNGED(remoterec))
			{
				/* This record has been completely deleted */
				SYNC_TRACE(5)
					cerr << "Deleting this record" << endl;
				pdb_DeleteRecordByID(_remotedb, remoterec->id);
			} else {
				struct pdb_record *newrec;

				SYNC_TRACE(5)
					cerr << "Saving this record" << endl;
				/* This record is merely new. Clear any
				 * dirty flags it might have, and add it to
				 * the local database.
				 */
				remoterec->attributes &=
					~(PDB_REC_EXPUNGED |
					  PDB_REC_DIRTY |
					  PDB_REC_DELETED |
					  PDB_REC_ARCHIVE);

				// First, make a copy
				newrec = pdb_CopyRecord(_remotedb, remoterec);
				if (newrec == 0)
				{
					cerr << "Can't copy a new record."
					     << endl;
					add_to_log("Error\n");
					return -1;
				}

				// Now add the copy to the local database
				pdb_AppendRecord(_localdb, newrec);
			}

			continue;
		}

		/* The remote record exists in the local database. */
		SYNC_TRACE(5)
		{
			cerr << "Local Record:" << endl;
			cerr << "\tID: 0x"
			     << hex << setw(8) << setfill('0')
			     << localrec->id << endl;
			cerr << "\tattributes: 0x" << hex << setw(2)
			     << setfill('0')
			     << static_cast<int>(localrec->attributes)
			     << " ";
			if (EXPUNGED(localrec))		cerr << "EXPUNGED ";
			if (DIRTY(localrec))		cerr << "DIRTY ";
			if (DELETED(localrec))		cerr << "DELETED ";
			if (PRIVATE(localrec))		cerr << "PRIVATE ";
			if (ARCHIVE(localrec))		cerr << "ARCHIVE ";
			cerr << endl;
			debug_dump(stderr, "LOC", localrec->data,
				   localrec->data_len > 64 ?
					64 : localrec->data_len);
		}

		/* This remote record exists in the local database. The
		 * main problem here, since there has been a sync that we
		 * don't know about, is that we can't trust the DIRTY flag
		 * on the remote record: if it's set, that means that the
		 * record is dirty, but if it's not set, that doesn't mean
		 * that the record isn't dirty.
		 * We get around this problem by comparing the contents of
		 * the two records, byte by byte. If the contents differ,
		 * then the remote record is dirty (the local one may be
		 * dirty, too, but that's irrelevant.
		 */
		if (!DIRTY(remoterec))
		{
			if (this->compare_rec(localrec, remoterec) != 0)
				/* The records are different. Mark the
				 * remote record as dirty.
				 */
				remoterec->attributes |= PDB_REC_DIRTY;
		}

		/* Sync the two records */
		err = this->SyncRecord(dbh, _localdb, localrec, remoterec);
		SYNC_TRACE(5)
			cerr << "SyncRecord returned " << err << endl;

		/* Mark the remote record as clean for the next phase */
		remoterec->attributes &=
			~(PDB_REC_EXPUNGED |
			  PDB_REC_DIRTY |
			  PDB_REC_DELETED |
			  PDB_REC_ARCHIVE);
	}

	/* Look up each record in the local database and see if it exists
	 * in the remote database. If it does, then we've dealt with it
	 * above. Otherwise, it's a new record in the local database.
	 */
	SYNC_TRACE(3)
		cerr << "Checking local database entries." << endl;

	struct pdb_record *nextrec;	// Next record in list

	for (localrec = _localdb->rec_index.rec, nextrec = 0;
	     localrec != 0;
	     localrec = nextrec)
	{
		/* 'localrec' might get deleted, so we need to make a note
		 * of the next record in the list now.
		 */
		nextrec = localrec->next;
		/* Try to look this record up in the remote database. */
		remoterec = pdb_FindRecordByID(_remotedb, localrec->id);
		if (remoterec != 0)
		{
			SYNC_TRACE(4)
				cerr << "Seen this record already" << endl;
			/* This local record exists in the remote
			 * database. We've dealt with it already, so
			 * we can skip it now.
			 */
			continue;
		}

		/* This record doesn't exist in the remote database. Deal
		 * with the various possibilities.
		 */

		if (DELETED(localrec) &&
		    (ARCHIVE(localrec) || !EXPUNGED(localrec)))
		{
			/* The local record was deleted, and needs to be
			 * archived.
			 */
			localrec->attributes &=
				~(PDB_REC_EXPUNGED |
				  PDB_REC_DIRTY |
				  PDB_REC_DELETED |
				  PDB_REC_ARCHIVE);

			// Archive this record
			SYNC_TRACE(5)
				cerr << "Archiving this record" << endl;
			this->archive_record(localrec);
		} else if (EXPUNGED(localrec))
		{
			/* The local record has been completely deleted */
			SYNC_TRACE(5)
				cerr << "Deleting this record" << endl;
			pdb_DeleteRecordByID(_localdb, localrec->id);
		} else if (DIRTY(localrec))
		{
			udword newID;	// ID of uploaded record

			/* This record is merely new. Clear any dirty flags
			 * it might have, and upload it to the Palm.
			 */
			localrec->attributes &=
				~(PDB_REC_EXPUNGED |
				  PDB_REC_DIRTY |
				  PDB_REC_DELETED |
				  PDB_REC_ARCHIVE);

			SYNC_TRACE(6)
				cerr << "> Sending local record to Palm"
				     << endl;
			err = DlpWriteRecord(_pconn, dbh, 0x80,
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
				cerr << "Error uploading record "
				     << hex << setw(8) << setfill('0')
				     << localrec->id
				     << ": " << err << endl;
				return -1;
			}

			/* The record was assigned a (possibly new) unique
			 * ID when it was uploaded. Make sure the local
			 * database reflects this.
			 */
			localrec->id = newID;
			SYNC_TRACE(7)
				cerr << "newID == "
				     << hex << setw(8) << setfill('0')
				     << newID << endl;
		} else {
			/* This record is clean but doesn't exist on the
			 * Palm. Archive it, then delete it.
			 * The most likely explanation is that the record
			 * was deleted, after which the user synced with
			 * some other machine, so the record got archived
			 * on that other machine, if it needed to be
			 * archived.
			 * However, the other possibility is that the user
			 * lost his Palm, got a replacement, and is now
			 * syncing with this machine. This is not the Right
			 * Thing to do, but it's a natural mistake, so
			 * archive the record just in case.
			 */
			this->archive_record(localrec);
			pdb_DeleteRecordByID(_localdb, localrec->id);
		}
	}

	/* Write the local database to the backup file */
	err = this->write_backup(_localdb);
	if (err < 0)
	{
		cerr << "GenericConduit::SlowSync: Can't write backup file."
		     << endl;
		err = DlpCloseDB(_pconn, dbh);	// Close the database
		add_to_log("Error\n");
		return -1;
	}

	/* Post-sync cleanup */
	SYNC_TRACE(3)
		cerr << "### Cleaning up database." << endl;
	err = DlpCleanUpDatabase(_pconn, dbh);
	if (err != DLPSTAT_NOERR)
	{
		cerr << "GenericConduit error: Can't clean up database: "
		     << err << endl;
		err = DlpCloseDB(_pconn, dbh);
		add_to_log("Error\n");
		return -1;
	}

	SYNC_TRACE(3)
		cerr << "### Resetting sync flags." << endl;
	err = DlpResetSyncFlags(_pconn, dbh);
	if (err != DLPSTAT_NOERR)
	{
		cerr << "GenericConduit error: Can't reset sync flags: "
		     << err << endl;
		err = DlpCloseDB(_pconn, dbh);
		add_to_log("Error\n");
		return -1;
	}

	/* Clean up */
	err = DlpCloseDB(_pconn, dbh);	// Close the database

	add_to_log("OK\n");
	return 0;
}

/* FastSync
 * A fast sync is done when the last machine that the Palm synced with
 * is this one. The record flags can be trusted, and we can just check
 * the records that have been marked as modified.
 */
int
GenericConduit::FastSync()
{
	int err;
	struct dlp_recinfo recinfo;	// Next modified record
	const ubyte *rptr;		// Pointer into buffers,
					// for reading
	ubyte dbh;			// Database handle
	struct pdb_record *remoterec;	// Record in remote database
	struct pdb_record *localrec;	// Record in local database

	add_to_log(_dbinfo->name);
	add_to_log(" - ");

	err = DlpOpenConduit(_pconn);
	if (err != DLPSTAT_NOERR)
	{
		SYNC_TRACE(4)
			cerr << "GenericConduit::FastSync: DlpOpenConduit() returned "
			     << err << endl;
		add_to_log("Error\n");
		return -1; 
	}

	/* Open the database for reading */
	err = DlpOpenDB(_pconn,
			CARD0,
			_dbinfo->name,
			DLPCMD_MODE_READ |
			(_dbinfo->db_flags & DLPCMD_DBFLAG_OPEN ?
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
		/* Can't complete this particular operation, but it's
		 * not a show-stopper. The sync can go on.
		 */
		cerr << "GenericConduit::FirstSync: Can't open \""
		     << _dbinfo->name << "\": "
		     << err << endl;
		add_to_log("Error\n");
		return -1;
	    default:
		/* Some other error, which probably means the sync
		 * can't continue.
		 */
		cerr << "GenericConduit::FirstSync: Can't open \""
		     << _dbinfo->name << "\": "
		     << err << endl;
		add_to_log("Error\n");
		return -1;
	}

	/* Read each modified record in turn. */
	while ((err = DlpReadNextModifiedRec(_pconn, dbh,
					     &recinfo, &rptr))
	       == DLPSTAT_NOERR)
	{
		/* Got the next modified record. Deal with it */
		/* XXX - Get category */
		remoterec = new_Record(recinfo.attributes,
				       recinfo.id,
				       recinfo.size,
				       rptr);
		if (remoterec == 0)
		{
			cerr << "GenericConduit Error: can't allocate new record."
			     << endl;
			this->close_archive();

			DlpCloseDB(_pconn, dbh);
			add_to_log("Error\n");
			return -1;
		}
		SYNC_TRACE(5)
		cerr << "Created new record from downloaded record" << endl;

		/* Look up the modified record in the local database
		 * Contract: 'localrec', here, is effectively
		 * read-only: it is only a pointer into the local
		 * database. It will be SyncRecord()'s responsibility
		 * to free it if necessary, so this function should
		 * not.
		 * However, 'remoterec' is "owned" by this function,
		 * so this function should free it if necessary.
		 * SyncRecord() may not.
		 */
		localrec = pdb_FindRecordByID(_localdb, remoterec->id);
		if (localrec == 0)
		{
			SYNC_TRACE(6)
				cerr << "localrec == 0" << endl;
			/* This record is new. Add it to the local
			 * database.
			 */
			/* XXX - Actually, the record may have been
			 * created and deleted since the last sync. In
			 * particular, if it's been deleted and marked
			 * for archival, it needs to go in the archive
			 * file, not the backup file.
			 * Alternately, just don't clear the flags:
			 * then it'll get picked up and get deleted or
			 * archived in the next phase.
			 */
			/* Clear the flags in remoterec before adding
			 * it to the local database: it's fresh and
			 * new.
			 */
			remoterec->attributes &= 0x0f;
				/* XXX - Presumably, this should just
				 * become a zero assignment, when/if
				 * attributes and categories get
				 * separated.
				 */

			/* Add the new record to localdb */
			if ((err = pdb_AppendRecord(_localdb, remoterec)) < 0)
			{
				cerr << "GenericConduit::FastSync: Can't append new record to database: "
				     << err << endl;
				pdb_FreeRecord(remoterec);
				DlpCloseDB(_pconn, dbh);
				add_to_log("Error\n");
				return -1;
			}

			/* Success. Go on to the next modified record */
			continue;
		}

		/* This record already exists in localdb. */
		SYNC_TRACE(5)
			cerr << "Found record by ID ("
			     << hex << setw(8) << setfill('0')
			     << remoterec->id << ")" << endl;
		err = this->SyncRecord(dbh,
				       _localdb, localrec,
				       remoterec);
		SYNC_TRACE(6)
			cerr << "SyncRecord() returned " << err << endl;
		if (err < 0)
		{
			SYNC_TRACE(6)
				cerr << "GenericConduit Error: SyncRecord returned "
				     << err << endl;
			DlpCloseDB(_pconn, dbh);

			this->close_archive();
			return -1;
		}

		pdb_FreeRecord(remoterec);
		// We're done with this record
	}

	/* DlpReadNextModifiedRec() returned an error code. See what
	 * it was and deal accordingly.
	 */
	switch (err)
	{
	    case DLPSTAT_NOTFOUND:
		/* No more modified records found. Skip to the next
		 * part.
		 */
		SYNC_TRACE(5)
			cerr << "GenericConduit: no more modified records."
			     << endl;
		break;
	    default:
		SYNC_TRACE(6)
			cerr << "GenericConduit: DlpReadNextModifiedRec returned "
			     << err << endl;
		DlpCloseDB(_pconn, dbh);

		this->close_archive();
		return -1;
	}

	/* XXX - Go through the local database and cope with new,
	 * modified, and deleted records.
	 */

	/* Write the local database to the backup file */
	err = this->write_backup(_localdb);
	if (err < 0)
	{
		cerr << "GenericConduit::SlowSync: Can't write backup file."
		     << endl;
		err = DlpCloseDB(_pconn, dbh);	// Close the database
		add_to_log("Error\n");
		return -1;
	}

	/* Post-sync cleanup */
	if (!DBINFO_ISRSRC(_dbinfo))
	{
		/* Actually, this function doesn't get called for
		 * resource databases, so the test above really checks
		 * a "can't possibly happen" condition.
		 */
		SYNC_TRACE(3)
			cerr << "### Cleaning up database." << endl;
		err = DlpCleanUpDatabase(_pconn, dbh);
		if (err != DLPSTAT_NOERR)
		{
			cerr << "GenericConduit error: Can't clean up database: "
			     << err << endl;
			err = DlpCloseDB(_pconn, dbh);
			add_to_log("Error\n");
			return -1;
		}
	}

	SYNC_TRACE(3)
		cerr << "### Resetting sync flags." << endl;
	err = DlpResetSyncFlags(_pconn, dbh);
	if (err != DLPSTAT_NOERR)
	{
		cerr << "GenericConduit error: Can't reset sync flags: "
		     << err << endl;
		err = DlpCloseDB(_pconn, dbh);
		add_to_log("Error\n");
		return -1;
	}

	/* Clean up */
	err = DlpCloseDB(_pconn, dbh);	// Close the database

	add_to_log("OK\n");
	return 0;		// Success
}

/* GenericConduit::SyncRecord
 * Sync a record in the local database with a remote record.
 *
 * Contract: This function is responsible for freeing 'localrec' if
 * necessary. However, it may not free 'remoterec'.
 */
int
GenericConduit::SyncRecord(
	ubyte dbh,
	struct pdb *localdb,
	struct pdb_record *localrec,
	const struct pdb_record *remoterec)
{
	int err;

	/* Figure out what to do with these records. This is long
	 * and hairy, but the basic form is:
	 *	check each possible case for 'remoterec'
	 *	    check each possible case for 'localrec'
	 *
	 * For each record, there are only four cases to consider:
	 * - DELETED ARCHIVE
	 *	This record has been deleted with archving.
	 * - (DELETED) EXPUNGE
	 *	This record has been deleted without archiving.
	 * - DIRTY
	 *	This record has been modified.
	 * - nothing
	 *	This record hasn't changed.
	 * All other flag combinations are either absurd or redundant. They
	 * should probably be flagged.
	 *
	 * Note: apparently, not all applications are polite enough to set
	 * the DELETED flag when a record is expunged, so need to just
	 * check that flag by itself. However, the ARCHIVE flag overlaps
	 * the category field, so a record that has been deleted with
	 * archiving must have both the DELETED and ARCHIVE flags set.
	 *
	 * If DELETED is set, but neither EXPUNGE nor ARCHIVE are set, then
	 * treat the record as if the ARCHIVE flag had been set.
	 * XXX - Rewrite all of the conditionals to deal with this case.
	 */
	if (DELETED(remoterec) && ARCHIVE(remoterec))
	{
		SYNC_TRACE(5)
			cerr << "Remote: deleted, archived"
			     << endl;
		/* Remote record has been deleted; user wants an
		 * archive copy.
		 */
		if (DELETED(localrec) && ARCHIVE(localrec))
		{
			SYNC_TRACE(5)
				cerr << "Local:  deleted, archived"
				     << endl;
			/* Local record has been deleted; user wants an
			 * archive copy.
			 */

			/* If the contents are identical, archive one copy.
			 * Otherwise, archive both copies.
			 */
			if ((localrec->data_len == remoterec->data_len) &&
			    (memcmp(localrec->data,
				    remoterec->data,
				    localrec->data_len) == 0))
			{
				/* The records are identical */
				this->archive_record(localrec);
			} else {
				/* The records have both been modified, but
				 * in different ways. Archive both of them.
				 */
				this->archive_record(localrec);
				this->archive_record(remoterec);
			}

			/* Delete the record on the Palm */
			SYNC_TRACE(6)
				cerr << "> Deleting record on Palm"
				     << endl;
			err = DlpDeleteRecord(_pconn, dbh, 0,
					      remoterec->id);
			if (err != DLPSTAT_NOERR)
			{
				cerr << "SlowSync: Warning: "
					"can't delete record "
				     << hex << setw(8) << setfill('0')
				     << remoterec->id
				     << ": " << err << endl;
				/* XXX - For now, just ignore this,
				 * since it's probably not a show
				 * stopper.
				 */
			}

			/* Delete localrec */
			SYNC_TRACE(6)
				cerr << "> Deleting record in local database"
				     << endl;
			pdb_DeleteRecordByID(localdb, localrec->id);

		} else if (/*DELETED(localrec) && */EXPUNGED(localrec))
		{
			/* Local record has been deleted without a trace. */
			SYNC_TRACE(5)
				cerr << "Local:  deleted, expunged"
				     << endl;

			/* Archive remoterec */
			SYNC_TRACE(6)
				cerr << "> Archiving remote record"
				     << endl;
			this->archive_record(remoterec);

			/* Delete the record on the Palm */
			SYNC_TRACE(6)
				cerr << "> Deleting record on Palm"
				     << endl;
			err = DlpDeleteRecord(_pconn, dbh, 0,
					      remoterec->id);
			if (err != DLPSTAT_NOERR)
			{
				cerr << "SlowSync: Warning: "
					"can't delete record "
				     << hex << setw(8) << setfill('0')
				     << remoterec->id
				     << ": " << err << endl;
				/* XXX - For now, just ignore this,
				 * since it's probably not a show
				 * stopper.
				 */
			}

			/* Delete localrec */
			SYNC_TRACE(6)
				cerr << "> Deleting record in local database"
				     << endl;
			pdb_DeleteRecordByID(localdb, localrec->id);

		} else if (DIRTY(localrec))
		{
			udword newID;	/* ID of uploaded record */

			/* Local record has changed */
			SYNC_TRACE(5)
				cerr << "Local:  dirty"
				     << endl;

			/* Archive remoterec */
			SYNC_TRACE(6)
				cerr << "> Archiving remote record"
				     << endl;
			this->archive_record(remoterec);

			/* Fix flags */
			localrec->attributes &= 0x0f;
				/* XXX - This will just be set to 0, once
				 * pdb_record has separate fields for
				 * attributes and category.
				 */
				/* XXX - Actually, don't clear the PRIVATE
				 * flag.
				 */

			/* Upload localrec to Palm */
			SYNC_TRACE(6)
				cerr << "> Sending local record to Palm"
				     << endl;
			err = DlpWriteRecord(_pconn, dbh, 0x80,
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
				cerr << "Error uploading record "
				     << hex << setw(8) << setfill('0')
				     << localrec->id
				     << ": " << err << endl;
				return -1;
			}

			/* The record was assigned a (possibly new) unique
			 * ID when it was uploaded. Make sure the local
			 * database reflects this.
			 */
			localrec->id = newID;
			SYNC_TRACE(7)
				cerr << "newID == "
				     << hex << setw(8) << setfill('0')
				     << newID << endl;

		} else {
			/* Local record hasn't changed */
			SYNC_TRACE(5)
				cerr << "Local:  clean"
				     << endl;

			/* Archive remoterec */
			SYNC_TRACE(6)
				cerr << "> Archiving remote record"
				     << endl;
			this->archive_record(localrec);

			/* Delete localrec */
			SYNC_TRACE(6)
				cerr << "> Deleting record in local database"
				     << endl;
			pdb_DeleteRecordByID(localdb, localrec->id);

		}
	} else if (/*DELETED(remoterec) && */EXPUNGED(remoterec))
	{
		/* Remote record has been deleted without a trace. */
		SYNC_TRACE(5)
			cerr << "Remote: deleted, expunged"
			     << endl;

		if (DELETED(localrec) && ARCHIVE(localrec))
		{
			/* Local record has been deleted; user wants an
			 * archive copy.
			 */
			SYNC_TRACE(5)
				cerr << "Local:  deleted, archived"
				     << endl;

			/* Fix flags */
			localrec->attributes &= 0x0f;

			/* Archive localrec */
			SYNC_TRACE(6)
				cerr << "> Archiving local record"
				     << endl;
			this->archive_record(localrec);

			/* Delete localrec */
			SYNC_TRACE(6)
				cerr << "> Deleting record in local database"
				     << endl;
			pdb_DeleteRecordByID(localdb, localrec->id);

		} else if (/*DELETED(localrec) && */EXPUNGED(localrec))
		{
			/* Local record has been deleted without a trace. */
			SYNC_TRACE(5)
				cerr << "Local:  deleted, expunged" << endl;

			/* Delete localrec */
			SYNC_TRACE(6)
				cerr << "> Deleting record in local database"
				     << endl;
			pdb_DeleteRecordByID(localdb, localrec->id);

		} else if (DIRTY(localrec))
		{
			/* Local record has changed */

			udword newID;	/* ID of uploaded record */

			SYNC_TRACE(5)
				cerr << "Local:  dirty" << endl;

			/* Delete remoterec */
			SYNC_TRACE(6)
				cerr << "> Deleting remote record"
				     << endl;
			err = DlpDeleteRecord(_pconn, dbh, 0,
					      remoterec->id);
			if (err != DLPSTAT_NOERR)
			{
				cerr << "SlowSync: Warning: can't delete record "
				     << hex << setw(8) << setfill('0')
				     << remoterec->id << ": " << err << endl;
				/* XXX - For now, just ignore this,
				 * since it's probably not a show
				 * stopper.
				 */
			}

			/* Fix flags */
			localrec->attributes &= 0x0f;
				/* XXX - This will just be 0 once
				 * pdb_record includes separate fields for
				 * attributes and category.
				 */

			/* Upload localrec to Palm */
			SYNC_TRACE(6)
				cerr << "> Uploading local record to Palm"
				     << endl;
			err = DlpWriteRecord(_pconn, dbh, 0x80,
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
				cerr << "Error uploading record "
				     << hex << setw(8) << setfill('0')
				     << localrec->id
				     << ": " << err << endl;
				return -1;
			}

			/* The record was assigned a (possibly new) unique
			 * ID when it was uploaded. Make sure the local
			 * database reflects this.
			 */
			localrec->id = newID;
			SYNC_TRACE(7)
				cerr << "newID == "
				     << hex << setw(8) << setfill('0')
				     << newID << endl;

		} else {
			/* Local record hasn't changed */
			SYNC_TRACE(5)
				cerr << "Local:  clean"
				     << endl;

			/* Delete localrec */
			SYNC_TRACE(6)
				cerr << "> Deleting record in local database"
				     << endl;
			pdb_DeleteRecordByID(localdb, localrec->id);

		}
	} else if (DIRTY(remoterec))
	{
		/* Remote record has changed */
		SYNC_TRACE(5)
			cerr << "Remote: dirty"
			     << endl;

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

			SYNC_TRACE(5)
				cerr << "Local:  deleted, archived"
				     << endl;

			/* Fix flags */
			localrec->attributes &= 0x0f;
				/* XXX - This will just be 0 once
				 * pdb_record includes separate fields for
				 * attributes and category.
				 */

			/* Archive localrec */
			SYNC_TRACE(6)
				cerr << "> Archiving local record"
				     << endl;
			this->archive_record(localrec);

			/* Copy remoterec to localdb */
			SYNC_TRACE(6)
				cerr << "> Copying remote record to local database"
				     << endl;
			/* XXX - Get category */
			newrec = new_Record(remoterec->attributes,
					    remoterec->id,
					    remoterec->data_len,
					    remoterec->data);
			if (newrec == 0)
			{
				cerr << "SyncRecord: can't copy record"
				     << endl;
				return -1;
			}

			/* Fix flags */
			localrec->attributes &= 0x0f;
				/* XXX - This will just be 0 once
				 * pdb_record includes separate fields for
				 * attributes and category.
				 */

			pdb_AppendRecord(localdb, newrec);

		} else if (/*DELETED(localrec) && */EXPUNGED(localrec))
		{
			/* Local record has been deleted without a trace. */
			struct pdb_record *newrec;

			SYNC_TRACE(5)
				cerr << "Local:  deleted, expunged"
				     << endl;

			/* Delete localrec */
			SYNC_TRACE(6)
				cerr << "> Deleting local record"
				     << endl;
			pdb_DeleteRecordByID(localdb, localrec->id);

			/* Copy remoterec to localdb */
			SYNC_TRACE(6)
				cerr << "> Copying remote record to local database"
				     << endl;
			/* XXX - Get category */
			newrec = new_Record(remoterec->attributes,
					    remoterec->id,
					    remoterec->data_len,
					    remoterec->data);
			if (newrec == 0)
			{
				cerr << "SyncRecord: can't copy record"
				     << endl;
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
			SYNC_TRACE(5)
				cerr << "Local:  dirty" << endl;

			/* See if the records are identical */
			/* XXX - Use method to compare records */
			if ((localrec->data_len == remoterec->data_len) &&
			    (localrec->data != 0) &&
			    (remoterec->data != 0) &&
			    (memcmp(localrec->data, remoterec->data,
				    localrec->data_len) == 0))
			{
				/* The records are identical.
				 * Reset localrec's flags to clean, but
				 * otherwise do nothing.
				 */
				localrec->attributes &= 0x0f;
					/* XXX - This will just be 0 once
					 * pdb_record includes separate
					 * fields for attributes and
					 * category.
					 */
			} else {
				/* The records have both been modified, but
				 * in different ways.
				 */
				udword newID;	/* ID of uploaded record */
				struct pdb_record *newrec;

				/* Fix flags on localrec */
				localrec->attributes &= 0x0f;
					/* XXX - This will just be 0 once
					 * pdb_record includes separate
					 * fields for attributes and
					 * category.
					 */

				/* Upload localrec to Palm */
				SYNC_TRACE(6)
					cerr << "> Uploading local record to Palm"
					     << endl;
				err = DlpWriteRecord(_pconn, dbh, 0x80,
						     localrec->id,
						     /* XXX - The bottom
						      * nybble of the
						      * attributes byte is
						      * the category. Fix
						      * this throughout.
						      */
						     localrec->attributes & 0xf0,
						     localrec->attributes & 0xf0,
						     localrec->data_len,
						     localrec->data,
						     &newID);
				if (err != DLPSTAT_NOERR)
				{
					cerr << "Error uploading record "
					     << hex << setw(8) << setfill('0')
					     << localrec->id
					     << ": " << err << endl;
					return -1;
				}

				/* The record was assigned a (possibly new)
				 * unique ID when it was uploaded. Make
				 * sure the local database reflects this.
				 */
				localrec->id = newID;
				SYNC_TRACE(7)
					cerr << "newID == "
					     << hex << setw(8) << setfill('0')
					     << newID << endl;

				/* Add remoterec to local database */
				SYNC_TRACE(7)
					cerr << "Adding remote record to local database."
					     << endl;
				/* First, make a copy (with clean flags) */
				/* XXX - Get category */
				newrec = new_Record(
					remoterec->attributes & 0x0f,
					remoterec->id,
					remoterec->data_len,
					remoterec->data);
				if (newrec == 0)
				{
					cerr << "Can't copy a new record."
					     << endl;
					return -1;
				}

				/* Now add the copy to the local database */
				err = pdb_InsertRecord(localdb, localrec,
						       newrec);
				if (err < 0)
				{
					cerr << "SlowSync: Can't insert record "
					     << hex << setw(8) << setfill('0')
					     << newrec->id << endl;
					pdb_FreeRecord(newrec);
					return -1;
				}

			}

		} else {
			/* Local record has not changed */
			struct pdb_record *newrec;

			SYNC_TRACE(5)
				cerr << "Local:  clean"
				     << endl;

			SYNC_TRACE(6)
				cerr << "> Replacing local record with remote one"
				     << endl;
			/* Replace localrec with remoterec.
			 * This is done in three stages:
			 * - copy 'remoterec' to 'newrec'
			 * - insert 'remoterec' after 'localrec'
			 * - delete 'localrec'
			 */
			SYNC_TRACE(6)
				cerr << "> Copying remote record to local database"
				     << endl;
			/* XXX - Get category */
			newrec = new_Record(remoterec->attributes,
					    remoterec->id,
					    remoterec->data_len,
					    remoterec->data);
			if (newrec == 0)
			{
				cerr << "SyncRecord: can't copy record"
				     << endl;
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
				cerr << "SlowSync: Can't insert record "
				     << hex << setw(8) << setfill('0')
				     << newrec->id << endl;
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
		SYNC_TRACE(5)
			cerr << "Remote: clean"
			     << endl;

		if (DELETED(localrec) && ARCHIVE(localrec))
		{
			/* Local record has been deleted; user wants an
			 * archive copy.
			 */
			SYNC_TRACE(5)
				cerr << "Local:  deleted, archived"
				     << endl;

			/* Fix local flags */
			localrec->attributes &= 0x0f;
				/* XXX - This will just be 0 once
				 * pdb_record includes separate fields for
				 * attributes and category.
				 */

			/* Archive localrec */
			SYNC_TRACE(6)
				cerr << "> Archiving local record"
				     << endl;
			this->archive_record(localrec);

			/* Delete localrec */
			SYNC_TRACE(6)
				cerr << "> Deleting record in local database"
				     << endl;
			pdb_DeleteRecordByID(localdb, localrec->id);

		} else if (/*DELETED(localrec) && */EXPUNGED(localrec))
		{
			/* Local record has been deleted without a trace. */
			SYNC_TRACE(5)
				cerr << "Local:  deleted, expunged"
				     << endl;

			/* Delete localrec */
			SYNC_TRACE(6)
				cerr << "> Deleting record in local database"
				     << endl;
			pdb_DeleteRecordByID(localdb, localrec->id);

		} else if (DIRTY(localrec))
		{
			/* Local record has changed */
			udword newID;	/* ID of uploaded record */

			SYNC_TRACE(5)
				cerr << "Local:  dirty"
				     << endl;

			/* Fix local flags */
			localrec->attributes &= 0x0f;
				/* XXX - This will just be 0 once
				 * pdb_record includes separate fields for
				 * attributes and category.
				 */

			/* Upload localrec to Palm */
			SYNC_TRACE(6)
				cerr << "> Uploading local record to Palm"
				     << endl;
			err = DlpWriteRecord(_pconn, dbh, 0x80,
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
				cerr << "Error uploading record "
				     << hex << setw(8) << setfill('0')
				     << localrec->id
				     << ": " << err << endl;
				return -1;
			}

			/* The record was assigned a (possibly new) unique
			 * ID when it was uploaded. Make sure the local
			 * database reflects this.
			 */
			localrec->id = newID;
			SYNC_TRACE(7)
				cerr << "newID == "
				     << hex << setw(8) << setfill('0')
				     << newID << endl;

		} else {
			/* Local record hasn't changed */
			SYNC_TRACE(5)
				cerr << "Local:  clean"
				     << endl;

			/* Do nothing */
			SYNC_TRACE(6)
				cerr << "> Not doing anything"
				     << endl;
		}
	}

	return 0;	/* Success */
}

/* GenericConduit::compare_rec
 * Compare two records, in the manner of 'strcmp()'. If rec1 is "less than"
 * rec2, return -1. If they are equal, return 0. If rec1 is "greater than"
 * rec2, return 1.
 * XXX - This is a naive and sub-optimal implementation. It remains to be
 * seen whether it's worth optimizing, though.
 */
int
GenericConduit::compare_rec(const struct pdb_record *rec1,
			    const struct pdb_record *rec2)
{
	int i;

	for (i = 0; i < rec1->data_len; ++i)
	{
		if (i >= rec2->data_len)
		{
			/* Got to end of rec2 before the end of rec1, but
			 * they've been equal so far, so rec2 is "smaller".
			 */
			return 1;
		}

		/* Compare the next byte */
		if (rec1->data[i] < rec2->data[i])
			/* rec1 is "less than" rec2 */
			return -1;
		else if (rec1->data[i] > rec2->data[i])
			/* rec1 is "greater than" rec2 */
			return 1;
	}

	/* The two records are equal over the entire length of rec1 */
	if (rec1->data_len < rec2->data_len)
		return 1;

	return 0;	/* Length is the same and data is the same */
}

/* GenericConduit::open_archive
 * A convenience function: records the fact that we're starting on a
 * database that might need to be archived. But don't do anything just yet,
 * since there may not be anything to archive, in which case we don't want
 * an empty archive file lying around.
 */
/* XXX - This approach is flawed, in that we may not find out until the
 * middle of the sync that the archive file can't be opened (e.g.,
 * permissions problem or something). It may be better to open the archive
 * file now and, if it had to be created, remember this fact. Then, if
 * nothing was written to it, delete it at the end.
 */
int
GenericConduit::open_archive()
{
	_archfd = -1;		// XXX - Should probably sanity-check
				// to see if it was already open

	return 0;
}

/* GenericConduit::archive_record
 * Archive the record 'rec'. If the archive file is not yet open, open it
 * now. If it doesn't exist, create it.
 */
int
GenericConduit::archive_record(const struct pdb_record *rec)
{
	struct arch_record arec;

	/* If no archive file has been opened yet, open one, creating it if
	 * necessary.
	 */
	if (_archfd < 0)
	{
		if ((_archfd = arch_open(_dbinfo->name, O_WRONLY)) < 0)
		{
			SYNC_TRACE(2)
				cerr << "Can't open \"" << _dbinfo->name
				     << "\". Attempting to create" << endl;
			if ((_archfd = arch_create(
				_dbinfo->name,
				_dbinfo)) < 0)
			{
				cerr << "Can't create \"" << _dbinfo->name
				     << "\"" << endl;
				return -1;
			}
		}
	}

	/* Write the record */
	arec.type = ARCHREC_REC;
	arec.data_len = rec->data_len;
	arec.data = rec->data;
	return arch_writerecord(_archfd, &arec);
}

/* GenericConduit::close_archive
 * A convenience function: we're done with the archive file. If nothing was
 * archived, do nothing. Otherwise, close the archive file and stuff.
 */
int
GenericConduit::close_archive()
{
	/* If an archive file was opened, close it.
	 */
	if (_archfd != -1)
		close(_archfd);

	_archfd = -1;

	return 0;
}

/* read_backup
 * Read the backup file and put it in _localdb. If the backup file doesn't
 * exist, that's not considered an error: just return 0, but leave _localdb
 * as having value 0 as well.
 * Returns 0 if successful, -1 in case of error.
 */
int
GenericConduit::read_backup()
{
	int err;
	char bakfname[MAXPATHLEN+1];	// Backup filename

	/* Construct the full pathname of the backup file */
	strncpy(bakfname, mkbakfname(_dbinfo), MAXPATHLEN);
	bakfname[MAXPATHLEN] = '\0';	// Terminate pathname, just in case

	/* See if the backup file exists */
	struct stat statbuf;

	err = stat(bakfname, &statbuf);
	if (err < 0)
	{
		SYNC_TRACE(3)
			perror("read_backup: stat");
		if (errno == ENOENT)
		{
			/* The backup file doesn't exist. This isn't
			 * technically an error.
			 */
			_localdb = 0;
			return 0;
		} 

		/* There's a serious problem: it's not just that the file
		 * doesn't exist; stat() failed for some other reason
		 * (e.g., directory permissions). This is serious enough to
		 * abort this conduit altogether.
		 */
		return -1;
	}

	/* Load backup file to _localdb */
	int infd;		// File descriptor for backup file
	if ((infd = open(bakfname, O_RDONLY)) < 0)
	{
		cerr << "read_backup error: Can't open " << bakfname << endl;
		return -1;
	}
	_localdb = pdb_Read(infd);
	close(infd);

	if (_localdb == 0)
	{
		cerr << "read_backup error: can't load \"" << bakfname
		     << '"' << endl;
		return -1; 
	}

	return 0;
}

/* write_backup
 * Write 'db' to the backup file. Returns 0 if successful, -1 in case
 * of error.
 * Writing the file is done in two parts: first, write the file to a
 * staging file. Then rename() the staging file to the real backup
 * file. That way, if there's an error halfway through writing the
 * file, the real backup doesn't get clobbered.
 */
int
GenericConduit::write_backup(struct pdb *db)
{
	int err;
	int outfd;			// Output file descriptor
	char bakfname[MAXPATHLEN+1];	// Name of output file
	char stage_fname[MAXPATHLEN+1];	// Name of staging file
	// XXX - ARGH. Really need to convert this XXXXXX into a unique
	// temporary file name. Or maybe just use mkstemp() later on.
	const char* stage_ext = ".XXXXXX";	// Staging file extension
	const int stage_ext_len = strlen(stage_ext);
					// Length of staging file extension

	/* Construct the full pathname of the backup file */
	strncpy(bakfname, mkbakfname(_dbinfo), MAXPATHLEN);
	bakfname[MAXPATHLEN] = '\0';	// Terminate pathname, just in case

	/* Construct the full pathname of the file we'll use for staging
	 * the write.
	 */
	strncpy(stage_fname, bakfname, MAXPATHLEN-stage_ext_len);
	strncat(stage_fname, stage_ext, stage_ext_len);
	if (mktemp(stage_fname) == 0)
	{
		cerr << "GenericConduit::write_backup: Can't create staging file name"
		     << endl;
		return -1;
	}

	/* Open the output file */
	if ((outfd = open(stage_fname,
			  O_WRONLY | O_CREAT | O_EXCL,
			  0600)) < 0)
	{
		cerr << "GenericConduit::write_backup: Can't create staging file \""
		     << stage_fname << "\"" << endl;
		return -1;
	}
	/* XXX - Lock the file */

	err = pdb_Write(db, outfd);
	if (err < 0)
	{
		cerr << "GenericConduit::write_backup: Can't write staging file "
		     << stage_fname << endl;
		close(outfd);
		return err;
	}
	close(outfd);

	/* Rename the staging file */
	err = rename(stage_fname, bakfname);
	if (err < 0)
	{
		cerr << "GenericConduit::write_backup: Can't rename staging file."
		     << endl
		     << "Backup left in " << stage_fname << ", hopefully"
		     << endl;
		return err;
	}

	return 0;		// Success
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
