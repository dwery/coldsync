/* GenericConduit.hh
 *
 * $Id: GenericConduit.hh,v 1.1 1999-07-04 13:40:32 arensb Exp $
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
	virtual int run();
	virtual int SyncRecord(ubyte dbh,
			       struct pdb *localdb,
			       struct pdb_record *localrec,
			       const struct pdb_record *remoterec);
	virtual int compare_rec(const struct pdb_record *rec1,
				const struct pdb_record *rec2);

	// XXX - Other methods that can be overridden
	// XXX - Methods for reading and writing backup file
	// XXX - Methods for dealing with archive file. Be sure to save
	// state (i.e., don't create an archive file until records are
	// actually archived)

    protected:
	struct PConnection *_pconn;
	struct Palm *_palm;
	struct dlp_dbinfo *_dbinfo;
	struct pdb *_localdb;		// Local database (from backup dir)
	struct pdb *_remotedb;		// Remote database (from Palm)

// XXX 	virtual int FirstSync();	// Sync a database for the first time
// XXX 	virtual int SlowSync();		// Do a slow sync
// XXX	virtual int FastSync();		// Do a fast sync
	virtual int open_archive();
	virtual int archive_record(const struct pdb_record *rec);
	virtual int close_archive();

    private:
	int _archfd;			// File descriptor for archive file
};

#endif	// _GenericConduit_hh_

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
