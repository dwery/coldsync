/* GenericConduit.cc
 *
 * Methods and such for the generic conduit.
 *
 * $Id: GenericConduit.cc,v 1.2 1999-07-04 02:27:32 arensb Exp $
 */
#include <iostream.h>
#include <iomanip.h>		// Probably only needed for debugging
#include <stdio.h>		// For perror()
				// XXX - This may turn out to be a Bad Thing
#include <stdlib.h>		// For free()
#include <fcntl.h>		// For open()
#include <sys/stat.h>
#include <sys/param.h>		// For MAXPATHLEN
#include <string.h>		// XXX - Is this C++-safe everywhere?
#include <errno.h>		// XXX - Is this C++-safe everywhere?
#include "GenericConduit.hh"

extern "C" {
#include "config.h"
#include "pconn/dlp_cmd.h"
#include "pconn/util.h"
#include "coldsync.h"
#include "archive.h"
#include "pdb.h"

extern int SyncRecord(struct PConnection *pconn,
		      ubyte dbh,
		      struct pdb *localdb,
		      struct pdb_record *localrec,
		      const struct pdb_record *remoterec);
extern int add_to_log(char *msg);
}

#define SYNC_DEBUG	1
#ifdef SYNC_DEBUG

extern int sync_debug;

// XXX - This needs to be redone, C++ style
#define SYNC_TRACE(level, format...) \
	if (sync_debug >= (level)) \
		fprintf(stderr, "SYNC:" format);
#endif	/* SYNC_DEBUG */

int
run_GenericConduit(
	struct PConnection *pconn,
	struct Palm *palm,
	struct dlp_dbinfo *dbinfo)
{
	GenericConduit gc(pconn, palm, dbinfo);

	return gc.run();
}

// GenericConduit::GenericConduit
// Constructor
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

/* XXX - Redo this: see where the redundancies and such lie. See how much
 * redundancy and such can be eliminated. See about splitting sensibly into
 * methods.
 */
/* XXX - Look through the interaction between this and SyncRecord(): in
 * particular, when a record is archived, one(?) of the records (localrec?)
 * gets free()d twice. Presumably need to set up a contract, e.g.,
 * SyncRecord() will not free anything that was passed to it.
 */
int
GenericConduit::run()
{
	int err;

add_to_log(_dbinfo->name);
add_to_log(" - ");
	/* See if it's a ROM database. If so, just ignore it, since it
	 * can't be modified and hence need not be synced.
	 */
	if (DBINFO_ISROM(_dbinfo))
	{
		// XXX - This ought to be configurable
cerr << "\"" << _dbinfo->name << "\" is a ROM database. Ignoring it." << endl;
		add_to_log("ROM\n"); 
		return 0; 
	}
cerr << "\"" << _dbinfo->name << "\" is not a ROM database." << endl;

	/* Resource databases are entirely different beasts. */
	if (DBINFO_ISRSRC(_dbinfo))
	{
		cerr << "I don't deal with resource databases." << endl;
		add_to_log("Not synced\n");
		return -1;
	}

	/* XXX - Actually, the entire business of loading the backup file
	 * ought to be handled by a read_backup() method. That way, it can
	 * easily be overridden to allow, say, loading from the net, or
	 * from a different file format.
	 */
	/* Construct the backup file name.
	 * In practice, there's probably no reason to make a copy of the
	 * string returned by mkbakfname(), but it seems safer, less prone
	 * to errors in case some day there are multiple conduits running
	 * concurrently.
	 */
	/* XXX - Should these be 'static'? How does that work across
	 * classes, and subclasses?
	 */
	char bakfname[MAXPATHLEN+1];	// Backup filename
	char stage_fname[MAXPATHLEN+1];	// Staging file name
	// XXX - ARGH. Really need to convert this XXXXXX into a unique
	// temporary file name. Or maybe just use mkstemp() later on.
	const char* stage_ext = ".XXXXXX";	// Staging file extension
	const int stage_ext_len = strlen(stage_ext);
					// Length of staging file extension
	int outfd;			// Output file descriptor
	struct pdb_record *remoterec;	// Record in remote database
	struct pdb_record *localrec;	// Record in local database

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
		cerr << "GenericConduit error: Can't create staging file name"
		     << endl;
		add_to_log("Error\n");
		return -1;
	}

	/* Open the output file. Open it now, so that if there are any
	 * errors, we don't waste time downloading the database.
	 */
	if ((outfd = open(stage_fname,
			  O_WRONLY | O_CREAT | O_EXCL,
			  0600)) < 0)
	{
		cerr << "GenericSync error: Can't create staging file \""
		     << stage_fname << "\"" << endl;
		add_to_log("Error\n");
		return -1;
	}

	/* See if the backup file exists */
	struct stat statbuf;

	err = stat(bakfname, &statbuf);
	if (err < 0)
	{
perror("GenericConduit: stat");
		/* The backup file doesn't exist. */
		if (errno != ENOENT)
		{
			/* There's a serious problem: it's not just that
			 * the file doesn't exist; stat() failed for some
			 * other reason (e.g., directory permissions). This
			 * is serious enough to abort this conduit
			 * altogether.
			 */
			cerr << "GenericConduit Error: can't stat() \""
			     << bakfname << "\". Aborting." << endl;
			add_to_log("Error\n");
			return -1;
		}

cerr << "Backup file doesn't exist. Doing a FirstSync()" << endl;

		/* Tell the Palm we're beginning a new sync. */
		ubyte dbh;		// Database handle

		err = DlpOpenConduit(_pconn);
		if (err != DLPSTAT_NOERR)
		{
cerr << "DlpOpenConduit() returned " << err << endl;
			add_to_log("Error\n");
			return -1; 
		}

		/* Open the database for reading */
		err = DlpOpenDB(_pconn,
				0,	// XXX - Card # shouldn't be hardcoded
				_dbinfo->name,
				DLPCMD_MODE_READ |
				(_dbinfo->db_flags & DLPCMD_DBFLAG_OPEN ?
				 0 :
				 DLPCMD_MODE_WRITE) |
				DLPCMD_MODE_SECRET,
					/* "Secret" records aren't actually
					 * secret. They're actually just
					 * the ones marked private, and
					 * they're not at all secret.
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
			cerr << "GenericConduit error: Can't open \""
			     << _dbinfo->name << "\": "
			     << err << endl;
			add_to_log("Error\n");
			return -1;
		    default:
			/* Some other error, which probably means the sync
			 * can't continue.
			 */
			cerr << "GenericConduit fatal error: Can't open \""
			     << _dbinfo->name << "\": "
			     << err << endl;
			add_to_log("Error\n");
			return -1;
		}

		this->open_archive();

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
		for (remoterec = _remotedb->rec_index.rec;
		     remoterec != 0;
		     remoterec = remoterec->next)
		{
			cerr << "Remote Record:" << endl;
			cerr << "\tID: 0x"
			     << hex << setw(8) << setfill('0')
			     << remoterec->id << endl;
			cerr << "\tattributes: 0x" << hex << setw(2)
			     << setfill('0')
			     << static_cast<int>(remoterec->attributes)
			     << " ";
			if (remoterec->attributes & PDB_REC_EXPUNGED)
				cerr << "EXPUNGED ";
			if (remoterec->attributes & PDB_REC_DIRTY)
				cerr << "DIRTY ";
			if (remoterec->attributes & PDB_REC_DELETED)
				cerr << "DELETED ";
			if (remoterec->attributes & PDB_REC_PRIVATE)
				cerr << "PRIVATE ";
			if (remoterec->attributes & PDB_REC_ARCHIVE)
				cerr << "ARCHIVE ";
			cerr << endl;

			// XXX - Probably ought to check for records that
			// have the DELETED flag, but neither EXPUNGED nor
			// ARCHIVE. Presumably, these should be archived,
			// just in case.
			if (remoterec->attributes & PDB_REC_ARCHIVE)
			{
				// Clear flags
				remoterec->attributes &=
					~(PDB_REC_EXPUNGED |
					  PDB_REC_DIRTY |
					  PDB_REC_DELETED |
					  PDB_REC_ARCHIVE);

				// Archive this record
				this->archive_record(remoterec);
cerr << "Ought to archive this record" << endl;
			} else if (remoterec->attributes & PDB_REC_EXPUNGED)
			{
				// Delete this record
cerr << "Deleting this record" << endl; 
				pdb_DeleteRecordByID(_remotedb,
						     remoterec->id);
			} else {
cerr << "Need to save this record" << endl; 
				// Clear flags
				remoterec->attributes &=
					~(PDB_REC_EXPUNGED |
					  PDB_REC_DIRTY |
					  PDB_REC_DELETED |
					  PDB_REC_ARCHIVE);
			}
		}

		this->close_archive();

		// Write the database to its backup file
		err = pdb_Write(_remotedb, outfd);
		if (err < 0)
		{
cerr << "GenericSync error: Can't write \"" << bakfname << '"' << endl;
			if (_remotedb != 0)
				free_pdb(_remotedb);
			_remotedb = 0;
			err = DlpCloseDB(_pconn, dbh);	// Close the database
			add_to_log("Error\n");
			return -1;
		}

		// Move staging output file to real backup file
		if ((err = rename(stage_fname, bakfname)) < 0)
		{
			cerr << "GenericSync error: Can't rename staging file."
			     << endl;
			perror("rename");
			if (_remotedb != 0)
				free_pdb(_remotedb);
			_remotedb = 0;
			err = DlpCloseDB(_pconn, dbh);
			add_to_log("Error\n");
			return -1;
		}

		/* Post-sync cleanup */
		if (!IS_RSRC_DB(_remotedb))
		{
			/* XXX - This function doesn't sync resource files,
			 * so shouldn't this if-statement be deleted?
			 */
cerr << "### Cleaning up database." << endl;
			err = DlpCleanUpDatabase(_pconn, dbh);
			if (err != DLPSTAT_NOERR)
			{
cerr << "GenericSync error: Can't clean up database: " << err << endl;
				free_pdb(_remotedb);
				err = DlpCloseDB(_pconn, dbh);
				add_to_log("Error\n");
				return -1;
			}
		}

cerr << "### Resetting sync flags." << endl;
		err = DlpResetSyncFlags(_pconn, dbh);
		if (err != DLPSTAT_NOERR)
		{
cerr << "GenericSync error: Can't reset sync flags: " << err << endl;
			free_pdb(_remotedb);
			err = DlpCloseDB(_pconn, dbh);
			add_to_log("Error\n");
			return -1;
		}

		/* Clean up */
		if (_remotedb != 0)
			free_pdb(_remotedb);
		_remotedb = 0;

		err = DlpCloseDB(_pconn, dbh);	// Close the database

		add_to_log("OK\n");
		return 0; 
	}

	/* Load backup file to localdb */
	int fd;			// File descriptor for backup file
	if ((fd = open(bakfname, O_RDONLY)) < 0)
	{
		cerr << "GenericSync error: Can't open " << bakfname << endl;
		add_to_log("Error\n");
		return -1;
	}
	_localdb = pdb_Read(fd);

	if (_localdb == 0)
	{
cerr << "GenericSync error: can't load \"" << bakfname << '"' << endl;
		add_to_log("Error\n");
		return -1; 
	}

	/* Open the database */
	ubyte dbh;		// Database handle

	err = DlpOpenConduit(_pconn);
	if (err != DLPSTAT_NOERR)
	{
cerr << "DlpOpenConduit() returned " << err << endl;
		free_pdb(_localdb);
		add_to_log("Error\n");
		return -1; 
	}

	/* Open the database for reading */
	err = DlpOpenDB(_pconn,
			0,	// XXX - Card # shouldn't be hardcoded
				// XXX - Define a "CARD0" constant, to make
				// it easy to find all of these hardcoded
				// card numbers when/if multiple memory
				// cards become supported.
			_dbinfo->name,
			DLPCMD_MODE_READ |
			(_dbinfo->db_flags & DLPCMD_DBFLAG_OPEN ?
			 0 :
			 DLPCMD_MODE_WRITE) |
			DLPCMD_MODE_SECRET,
			/* "Secret" records aren't actually secret. They're
			 * actually just the ones marked private, and
			 * they're not at all secret.
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
		cerr << "GenericConduit error: Can't open \""
		     << _dbinfo->name << "\": " << err << endl;
		free_pdb(_localdb);
		add_to_log("Error\n");
		return -1;
	    default:
		/* Some other error, which probably means the sync can't
		 * continue.
		 */
		cerr << "GenericConduit fatal error: Can't open \""
		     << _dbinfo->name << "\": " << err << endl;
		free_pdb(_localdb);
		add_to_log("Error\n");
		return -1;
	}

	if (global_opts.force_slow || need_slow_sync)
	{
cerr << "Doing a slow sync." << endl;
cerr << "*** Phase 1:" << endl;
		// Phase 1: grab the entire database from the Palm
		_remotedb = pdb_Download(_pconn, _dbinfo, dbh);
		if (_remotedb == 0)
		{
cerr << "GenericSync error: Can't download \"" << _dbinfo->name << '"' << endl;
			DlpCloseDB(_pconn, dbh);
			free_pdb(_localdb);
			add_to_log("Error\n");
			return -1;
		}

		/* Check each remote record in turn, and compare it
		 * to the copy in the local database.
		 */
cerr << "Checking remote database entries." << endl;
		for (remoterec = _remotedb->rec_index.rec;
		     remoterec != 0;
		     remoterec = remoterec->next)
		{
			cerr << "Remote Record:" << endl;
			cerr << "\tID: 0x"
			     << hex << setw(8) << setfill('0')
			     << remoterec->id << endl;
			cerr << "\tattributes: 0x" << hex << setw(2)
			     << setfill('0')
			     << static_cast<int>(remoterec->attributes)
			     << " ";
			if (remoterec->attributes & PDB_REC_EXPUNGED)
				cerr << "EXPUNGED ";
			if (remoterec->attributes & PDB_REC_DIRTY)
				cerr << "DIRTY ";
			if (remoterec->attributes & PDB_REC_DELETED)
				cerr << "DELETED ";
			if (remoterec->attributes & PDB_REC_PRIVATE)
				cerr << "PRIVATE ";
			if (remoterec->attributes & PDB_REC_ARCHIVE)
				cerr << "ARCHIVE ";
			cerr << endl;
			debug_dump(stderr, "REM", remoterec->data,
				   remoterec->data_len > 64 ? 64 : remoterec->data_len);

			/* Look the record up in the local database. */
			localrec = pdb_FindRecordByID(_localdb,
						      remoterec->id);
			if (localrec == 0)
			{
				/* This record is new. Mark it as modified
				 * in the remote database.
				 */
				cerr << "New record 0x" << hex
				     << setw(8) << setfill('0')
				     << remoterec->id
				     << " is new." << endl;
				remoterec->attributes |= PDB_REC_DIRTY;
				continue;
			}

			/* The remote record exists in the local database. */
			cerr << "Local Record:" << endl;
			cerr << "\tID: 0x"
			     << hex << setw(8) << setfill('0')
			     << localrec->id << endl;
			cerr << "\tattributes: 0x" << hex << setw(2)
			     << setfill('0')
			     << static_cast<int>(localrec->attributes)
			     << " ";
			if (localrec->attributes & PDB_REC_EXPUNGED)
				cerr << "EXPUNGED ";
			if (localrec->attributes & PDB_REC_DIRTY)
				cerr << "DIRTY ";
			if (localrec->attributes & PDB_REC_DELETED)
				cerr << "DELETED ";
			if (localrec->attributes & PDB_REC_PRIVATE)
				cerr << "PRIVATE ";
			if (localrec->attributes & PDB_REC_ARCHIVE)
				cerr << "ARCHIVE ";
			cerr << endl;
			debug_dump(stderr, "LOC", localrec->data,
				   localrec->data_len > 64 ? 64 : localrec->data_len);

			if (this->compare_rec(localrec, remoterec) == 0)
			{
				cerr << "The local and remote records are equal." << endl;
				/* Mark both of the records as clean */
				remoterec->attributes &= ~PDB_REC_DIRTY;
				localrec->attributes &= ~PDB_REC_DIRTY;
				continue;
			} else {
				cerr << "The local and remote records are NOT equal." << endl;
				/* Mark the remote record as dirty */
				remoterec->attributes |= PDB_REC_DIRTY;
				continue;
			}
		}

		/* Look up each record in the local database and
		 * see if it exists in the remote database.
		 */
		cerr << "Checking local database entries." << endl;
		for (localrec = _localdb->rec_index.rec;
		     localrec != 0;
		     localrec = localrec->next)
		{
			/* Try to look this record up in the remote
			 * database.
			 */
			remoterec = pdb_FindRecordByID(_remotedb,
						       localrec->id);
			if (remoterec != 0)
			{
				cerr << "Seen this record already" << endl;
				/* This local record exists in the remote
				 * database. We've dealt with it already,
				 * so we can skip it now.
				 */
				continue;
			}

			/* The local record doesn't exist in the remote
			 * database. If its dirty flag is set, that means
			 * it was added locally since the last sync, and
			 * should be added to the remote database. If the
			 * local record isn't dirty, that means it was
			 * deleted on the Palm (and, presumably, archived
			 * someplace else if it needed to be archived);
			 * delete it from the local database.
			 */
			/* XXX - This is incorrect behavior: Scenario:
			 * user's Palm is stolen. He gets a replacement and
			 * sync. Now the desktop has all of the bogus
			 * "Welcome" records, but all of the old data has
			 * been deleted.
			 * At the very least, should archive these records
			 * just in case. In the theft scenario, it would be
			 * best to upload them automatically, but this is
			 * wrong in the more ordinary case (user deleted
			 * them, ad they were archived elsewhere).
			 */
			if ((localrec->attributes & PDB_REC_DIRTY)
			    == 0)
			{
				/* Delete this record from the local
				 * database; presumably it was archived
				 * elsewhere.
				 */
				cerr << "Deleting this record: it's clean locally but doesn't exist in the remote database." << endl;
				pdb_DeleteRecordByID(_localdb, localrec->id);
			}
		}

		/* Phase 2:
		 * Now that each record in the remote database has been
		 * examined, and it has been decided whether it is new,
		 * deleted, etc., go through the database once again and do
		 * a proper sync, following the same logic as for fast
		 * syncs.
		 */
		/* XXX - It may be possible to fold phases 1 and 2 into
		 * one. But is this desirable?
		 */
		cerr << "*** Phase 2:" << endl;

		this->open_archive();

		for (remoterec = _remotedb->rec_index.rec;
		     remoterec != NULL;
		     remoterec = remoterec->next)
		{
			cerr << "Remote Record:" << endl;
			cerr << "\tID: 0x" << hex << setw(8) << setfill('0')
			     << remoterec->id << endl;
			cerr << "\tattributes: 0x" << hex << setw(2)
			     << setfill('0')
			     << static_cast<int>(remoterec->attributes)
			     << " ";
			if (remoterec->attributes & PDB_REC_EXPUNGED)
				cerr << "EXPUNGED ";
			if (remoterec->attributes & PDB_REC_DIRTY)
				cerr << "DIRTY ";
			if (remoterec->attributes & PDB_REC_DELETED)
				cerr << "DELETED ";
			if (remoterec->attributes & PDB_REC_PRIVATE)
				cerr << "PRIVATE ";
			if (remoterec->attributes & PDB_REC_ARCHIVE)
				cerr << "ARCHIVE ";
			cerr << endl;
debug_dump(stderr, "REM", remoterec->data,
	   remoterec->data_len > 64 ? 64 : remoterec->data_len);

			/* Find the local version of the record */
			localrec = pdb_FindRecordByID(_localdb, remoterec->id);
			if (localrec == 0)
			{
				struct pdb_record *newrec;

				/* This record is new. Add it to the local
				 * database.
				 */
cerr << "Adding record 0x" << hex << setw(8) << setfill('0')
     << remoterec->id << " to local database" << endl;

				/* Fix flags */
				remoterec->attributes &= 0x0f;
				/* XXX - This will just be set to 0 once
				 * pdb_record includes separate fields for
				 * attributes and category.
				 */
				/* XXX - Actually, the "archived" flag
				 * overlaps the high bit of the category
				 */
				/* First, make a copy */
				newrec = pdb_CopyRecord(_remotedb, remoterec);
				if (newrec == NULL)
				{
cerr << "Can't copy a new record." << endl;
					add_to_log("Error\n");
					return -1;
				}

				/* Now add the copy to the local database */
				pdb_AppendRecord(_localdb, newrec);

				continue;
			}

			cerr << "Local Record:" << endl;
			cerr << "\tID: 0x" << hex << setw(8) << setfill('0')
			     << localrec->id << endl;
			cerr << "\tattributes: 0x" << hex << setw(2)
			     << setfill('0')
			     << static_cast<int>(localrec->attributes)
			     << " ";
			if (localrec->attributes & PDB_REC_EXPUNGED)
				cerr << "EXPUNGED ";
			if (localrec->attributes & PDB_REC_DIRTY)
				cerr << "DIRTY ";
			if (localrec->attributes & PDB_REC_DELETED)
				cerr << "DELETED ";
			if (localrec->attributes & PDB_REC_PRIVATE)
				cerr << "PRIVATE ";
			if (localrec->attributes & PDB_REC_ARCHIVE)
				cerr << "ARCHIVE ";
			cerr << endl;

			err = this->SyncRecord(dbh, _localdb, localrec,
					       remoterec);
cerr << "SyncRecord returned " << err << endl;
		}
		this->close_archive();

		/* Write the local database to the backup file */
		err = pdb_Write(_localdb, outfd);
		if (err < 0)
		{
			cerr << "GenericSync error: Can't write \"" << bakfname << '"' << endl;
			if (_remotedb != 0)
				free_pdb(_remotedb);
			_remotedb = 0;

			if (_localdb != 0)
				free_pdb (_localdb);
			_localdb = 0;

			err = DlpCloseDB(_pconn, dbh);	// Close the database
			add_to_log("Error\n");
			return -1;
		}

		// Move staging output file to real backup file
		if ((err = rename(stage_fname, bakfname)) < 0)
		{
			cerr << "GenericSync error: Can't rename staging file."
			     << endl;
			perror("rename");
			if (_remotedb != 0)
				free_pdb(_remotedb);
			_remotedb = 0;
			err = DlpCloseDB(_pconn, dbh);
			add_to_log("Error\n");
			return -1;
		}

		/* Post-sync cleanup */
		if (!IS_RSRC_DB(_remotedb))
		{
cerr << "### Cleaning up database." << endl;
			err = DlpCleanUpDatabase(_pconn, dbh);
			if (err != DLPSTAT_NOERR)
			{
cerr << "GenericSync error: Can't clean up database: " << err << endl;
				free_pdb(_remotedb);
				err = DlpCloseDB(_pconn, dbh);
				add_to_log("Error\n");
				return -1;
			}
		}

cerr << "### Resetting sync flags." << endl;
		err = DlpResetSyncFlags(_pconn, dbh);
		if (err != DLPSTAT_NOERR)
		{
cerr << "GenericSync error: Can't reset sync flags: " << err << endl;
			free_pdb(_remotedb);
			err = DlpCloseDB(_pconn, dbh);
			add_to_log("Error\n");
			return -1;
		}
	} else {
		struct dlp_recinfo recinfo;	//Next modified record
		const ubyte *rptr;		// Pointer into buffers,
						// for reading

cerr << "Doing a fast sync." << endl;
		this->open_archive();

		while ((err = DlpReadNextModifiedRec(_pconn, dbh,
						     &recinfo, &rptr))
		       == DLPSTAT_NOERR)
		{
			/* Got the next modified record. Deal with it */
			remoterec = new_Record(recinfo.attributes,
					       recinfo.id,
					       recinfo.size,
					       rptr);
			if (remoterec == 0)
			{
				cerr << "GenericConduit Error: can't allocate new record." << endl;
				this->close_archive();

				if (_remotedb != 0) free_pdb(_remotedb);
				_remotedb = 0;

				if (_localdb != 0) free_pdb(_localdb);
				_localdb = 0;

				DlpCloseDB(_pconn, dbh);
				add_to_log("Error\n");
				return -1;
			}
cerr << "Created new record from downloaded record" << endl;

			/* Look up the modified record in the local database
			 * Contract: 'localrec', here, is effectively
			 * read-only: it is only a pointer into the local
			 * database. It will be SyncRecord()'s
			 * responsibility to free it if necessary, so this
			 * function should not.
			 * However, 'remoterec' is "owned" by this
			 * function, so this function should free it if
			 * necessary. SyncRecord() may not.
			 */
			localrec = pdb_FindRecordByID(_localdb, remoterec->id);
			if (localrec == 0)
			{
cerr << "localrec == 0" << endl;
				/* This record is new. Add it to the local
				 * database.
				 */
				/* XXX - Actually, the record may have been
				 * created and deleted since the last sync.
				 * In particular, if it's been deleted and
				 * marked for archival, it needs to go in
				 * the archive file, not the backup file.
				 */

				/* Clear the flags in remoterec before
				 * adding it to the local database: it's
				 * fresh and new.
				 */
				remoterec->attributes &= 0x0f;
				/* XXX - Presumably, this should just
				 * become a zero assignment, when/if
				 * attributes and categories get separated.
				 */

				/* Add the new record to localdb */
				if ((err = pdb_AppendRecord(_localdb,
							    remoterec)) < 0)
				{
cerr << "GenericSync error: Can't append new record to database: " << err << endl;
					pdb_FreeRecord(remoterec);

					if (_remotedb != 0)
						free_pdb(_remotedb);
					_remotedb = 0;

					if (_localdb != 0)
						free_pdb(_localdb);
					_localdb = 0;

					DlpCloseDB(_pconn, dbh);

					this->close_archive();
					add_to_log("Error\n");
					return -1;
				}

				/* Success. Go on to the next modified
				 * record */
				continue;
			}
cerr << "Found record by ID(0x" << hex << setw(8) << setfill('0') << remoterec->id << ")" << endl;

			/* This record already exists in localdb. */
			err = this->SyncRecord(dbh,
						_localdb, localrec,
						remoterec);
cerr << "SyncRecord() returned " << err << endl;
			if (err < 0)
			{
				cerr << "GenericSync Error: SyncRecord returned " << err << endl;
				if (_remotedb != 0)
					free_pdb(_remotedb);
				_remotedb = 0;

				if (_localdb != 0)
					free_pdb(_localdb);
				_localdb = 0;

				DlpCloseDB(_pconn, dbh);

				this->close_archive();
				add_to_log("Error\n");
				return -1;
			}

			pdb_FreeRecord(remoterec);
					// We're done with this record
		}

		/* DlpReadNextModifiedRec() returned an error code. See
		 * what it was and deal accordingly.
		 */
		switch (err)
		{
		    case DLPSTAT_NOTFOUND:
			/* No more modified records found. Skip to the next
			 * part */
cerr << "GenericSync: no more modified records." << endl;
			break;
		    default:
cerr << "GenericSync: DlpReadNextModifiedRec returned " << err << endl;

			if (_remotedb != 0)
				free_pdb(_remotedb);
			_remotedb = 0;

			if (_localdb != 0)
				free_pdb(_localdb);
			_localdb = 0;

			DlpCloseDB(_pconn, dbh);

			this->close_archive();
			add_to_log("Error\n");
			return -1;
		}

		/* XXX - Go through the local database and cope with new,
		 * modified, and deleted records.
		 */

		this->close_archive(); 

		/* Write the local database to the backup file */
		err = pdb_Write(_localdb, outfd);
		if (err < 0)
		{
cerr << "GenericSync error: Can't write \"" << bakfname << '"' << endl;
			if (_localdb != 0)
				free_pdb (_localdb);
			_localdb = 0;

			err = DlpCloseDB(_pconn, dbh);	// Close the database
			add_to_log("Error\n");
			return -1;
		}

		// Move staging output file to real backup file
		if ((err = rename(stage_fname, bakfname)) < 0)
		{
			cerr << "GenericSync error: Can't rename staging file."
			     << endl;
			perror("rename");
			if (_remotedb != 0)
				free_pdb(_remotedb);
			_remotedb = 0;
			err = DlpCloseDB(_pconn, dbh);
			add_to_log("Error\n");
			return -1;
		}

		/* Post-sync cleanup */
		if (!DBINFO_ISRSRC(_dbinfo))
		{
cerr << "### Cleaning up database." << endl;
			err = DlpCleanUpDatabase(_pconn, dbh);
			if (err != DLPSTAT_NOERR)
			{
cerr << "GenericSync error: Can't clean up database: " << err << endl;
				err = DlpCloseDB(_pconn, dbh);
				add_to_log("Error\n");
				return -1;
			}
		}

cerr << "### Resetting sync flags." << endl;
		err = DlpResetSyncFlags(_pconn, dbh);
		if (err != DLPSTAT_NOERR)
		{
cerr << "GenericSync error: Can't reset sync flags: " << err << endl;
			err = DlpCloseDB(_pconn, dbh);
			add_to_log("Error\n");
			return -1;
		}
	}

	/* Clean up */
	if (_remotedb != 0)	// XXX - This doesn't exist for fast sync
		free_pdb(_remotedb);
	_remotedb = 0;

	if (_localdb != 0)
		free_pdb(_localdb);
	_localdb = 0;

	err = DlpCloseDB(_pconn, dbh);
	add_to_log("OK\n");
	return 0;
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
	 */
/* Some convenience macros */
/* XXX - These should probably be 'static inline _expunged()' etc. */
#define EXPUNGED(r)	(((r)->attributes & PDB_REC_EXPUNGED) != 0)
#define DIRTY(r)	(((r)->attributes & PDB_REC_DIRTY) != 0)
#define DELETED(r)	(((r)->attributes & PDB_REC_DELETED) != 0)
#define ARCHIVE(r)	(((r)->attributes & PDB_REC_ARCHIVE) != 0)

	/* For each record, there are only four cases to consider:
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
			SYNC_TRACE(6, "> Deleting record on Palm\n");
			err = DlpDeleteRecord(_pconn, dbh, 0,
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

		} else if (/*DELETED(localrec) && */EXPUNGED(localrec))
		{
			/* Local record has been deleted without a trace. */
			SYNC_TRACE(5, "Local:  deleted, expunged\n");

			/* Archive remoterec */
			SYNC_TRACE(6, "> Archiving remote record\n");
			this->archive_record(remoterec);

			/* Delete the record on the Palm */
			SYNC_TRACE(6, "> Deleting record on Palm\n");
			err = DlpDeleteRecord(_pconn, dbh, 0,
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

			/* Archive remoterec */
			SYNC_TRACE(6, "> Archiving remote record\n");
			this->archive_record(remoterec);

			/* Fix flags */
			localrec->attributes &= 0x0f;
				/* XXX - This will just be set to 0, once
				 * pdb_record has separate fields for
				 * attributes and category.
				 */

			/* Upload localrec to Palm */
			SYNC_TRACE(6, "> Sending local record to Palm\n");
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

			/* Archive remoterec */
			SYNC_TRACE(6, "> Archiving remote record\n");
			this->archive_record(localrec);

			/* Delete localrec */
			SYNC_TRACE(6, "> Deleting record in local database\n");
			pdb_DeleteRecordByID(localdb, localrec->id);

		}
	} else if (/*DELETED(remoterec) && */EXPUNGED(remoterec))
	{
		/* Remote record has been deleted without a trace. */
		SYNC_TRACE(5, "Remote: deleted, expunged\n");

		if (DELETED(localrec) && ARCHIVE(localrec))
		{
			/* Local record has been deleted; user wants an
			 * archive copy.
			 */
			SYNC_TRACE(5, "Local:  deleted, archived\n");

			/* Fix flags */
			localrec->attributes &= 0x0f;

			/* Archive localrec */
			SYNC_TRACE(6, "> Archiving local record\n");
			this->archive_record(localrec);

			/* Delete localrec */
			SYNC_TRACE(6, "> Deleting record in local database\n");
			pdb_DeleteRecordByID(localdb, localrec->id);

		} else if (/*DELETED(localrec) && */EXPUNGED(localrec))
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
			err = DlpDeleteRecord(_pconn, dbh, 0,
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

			/* Fix flags */
			localrec->attributes &= 0x0f;
				/* XXX - This will just be 0 once
				 * pdb_record includes separate fields for
				 * attributes and category.
				 */

			/* Upload localrec to Palm */
			SYNC_TRACE(6, "> Uploading local record to Palm\n");
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

			/* Archive localrec */
			SYNC_TRACE(6, "> Archiving local record\n");
			this->archive_record(localrec);

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

		} else if (/*DELETED(localrec) && */EXPUNGED(localrec))
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

			/* See if the records are identical */
			if ((localrec->data_len == remoterec->data_len) &&
			    (localrec->data != NULL) &&
			    (remoterec->data != NULL) &&
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
				SYNC_TRACE(6, "> Uploading local record to Palm\n");
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
					fprintf(stderr,
						"Error uploading record 0x%08lx: %d\n",
						localrec->id,
						err);
					return -1;
				}

				/* The record was assigned a (possibly new)
				 * unique ID when it was uploaded. Make
				 * sure the local database reflects this.
				 */
				localrec->id = newID;
				SYNC_TRACE(7, "newID == 0x%08lx\n", newID);

				/* Add remoterec to local database */
				SYNC_TRACE(7, "Adding remote record to local database.\n");
				/* First, make a copy (with clean flags) */
				newrec = new_Record(
					remoterec->attributes & 0x0f,
					remoterec->id,
					remoterec->data_len,
					remoterec->data);
				if (newrec == NULL)
				{
					fprintf(stderr, "Can't copy a new record.\n");
					return -1;
				}

				/* Now add the copy to the local database */
				pdb_InsertRecord(localdb, localrec, newrec);
				if (err < 0)
				{
					fprintf(stderr, "SlowSync: Can't insert record 0x%08lx\n",
						newrec->id);
					pdb_FreeRecord(newrec);
					return -1;
				}

			}

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

			/* Archive localrec */
			SYNC_TRACE(6, "> Archiving local record\n");
			this->archive_record(localrec);

			/* Delete localrec */
			SYNC_TRACE(6, "> Deleting record in local database\n");
			pdb_DeleteRecordByID(localdb, localrec->id);

		} else if (/*DELETED(localrec) && */EXPUNGED(localrec))
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
cerr << "Can't open \"" << _dbinfo->name << "\". Attempting to create" << endl;
			if ((_archfd = arch_create(
				_dbinfo->name,
				_dbinfo)) < 0)
			{
cerr << "Can't create \"" << _dbinfo->name << "\"" << endl;
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

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
