/* GenericConduit.hh
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: GenericConduit.hh,v 1.3 1999-08-26 14:24:42 arensb Exp $
 */
#ifndef _GenericConduit_hh_
#define _GenericConduit_hh_

extern "C" {
#include "config.h"
#include "dlp_cmd.h"
#include "coldsync.h"
}

extern "C" {
extern int run_GenericConduit(struct PConnection *pconn,
			      struct Palm *palm,
			      struct dlp_dbinfo *db);
}

class GenericConduit
{
    public:
	GenericConduit(struct PConnection *pconn,
		       struct Palm *palm,
		       struct dlp_dbinfo *db);
	virtual ~GenericConduit();
	virtual int run();
	virtual int SyncRecord(ubyte dbh,
			       struct pdb *localdb,
			       struct pdb_record *localrec,
			       const struct pdb_record *remoterec);
	virtual int compare_rec(const struct pdb_record *rec1,
				const struct pdb_record *rec2);

    protected:
	struct PConnection *_pconn;
	struct Palm *_palm;
	struct dlp_dbinfo *_dbinfo;
	struct pdb *_localdb;		// Local database (from backup dir)
	struct pdb *_remotedb;		// Remote database (from Palm)

 	virtual int FirstSync();	// Sync a database for the first time
 	virtual int SlowSync();		// Do a slow sync
	virtual int FastSync();		// Do a fast sync
	virtual int open_archive();
	virtual int archive_record(const struct pdb_record *rec);
	virtual int close_archive();
	virtual int read_backup();	// Load backup file from disk
	virtual int write_backup(struct pdb *db);
					// Write backup file to disk
		/* XXX - Ideally, the conduit should open the staging
		 * output file first, to make sure that it's writable. That
		 * way, if it isn't, the conduit can fail immediately,
		 * rather than after download a bunch of records and
		 * wasting the user's time. However, this is a pathological
		 * case, so hopefully this can be put off until a later
		 * version. OTOH, shouldn't delay too long, since this'll
		 * involve messing with the API.
		 */
	/* XXX - Ought to be a function for determining whether _this_
	 * database needs a slow sync. This would involve keeping a list of
	 * which database was last synced with which machine.
	 */

    private:
	int _archfd;			// File descriptor for archive file
};

#endif	// _GenericConduit_hh_

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
