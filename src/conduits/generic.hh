/* GenericConduit.hh
 *
 *	Copyright (C) 1999-2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: generic.hh,v 1.1 2002-12-10 16:55:31 azummo Exp $
 */
#ifndef _GenericConduit_hh_
#define _GenericConduit_hh_

extern "C" {
#include "config.h"
#include "pconn/pconn.h"
#include "coldsync.h"
}

extern "C" {
extern int run_GenericConduit(PConnection *pconn,
			      const struct dlp_dbinfo *db,
			      const conduit_block *block,
			      const pda_block *pda);
}

class GenericConduit
{
    public:
	GenericConduit(PConnection *pconn,
		       const struct dlp_dbinfo *db);
	virtual ~GenericConduit(void);
	virtual int run(void);
	virtual int SyncRecord(ubyte dbh,
			       struct pdb *localdb,
			       struct pdb_record *localrec,
			       const struct pdb_record *remoterec);
	virtual int compare_rec(const struct pdb_record *rec1,
				const struct pdb_record *rec2);

    protected:
	PConnection *_pconn;
	const struct dlp_dbinfo *_dbinfo;
	struct pdb *_localdb;		// Local database (from backup dir)
	struct pdb *_remotedb;		// Remote database (from Palm)

 	virtual int FirstSync(void);	// Sync a database for the first time
 	virtual int SlowSync(void);	// Do a slow sync
	virtual int FastSync(void);	// Do a fast sync
	virtual int open_archive(void);
	virtual int archive_record(const struct pdb_record *rec);
	virtual int close_archive(void);
	virtual int read_backup(void);	// Load backup file from disk
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
