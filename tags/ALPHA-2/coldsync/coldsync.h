/* coldsync.h
 *
 * Data structures and such needed by 'coldsync'.
 *
 * $Id: coldsync.h,v 1.2 1999-02-22 10:38:21 arensb Exp $
 */
#ifndef _coldsync_h_
#define _coldsync_h_

#include "pconn/PConnection.h"

#define BACKUP_DIR	"palm_backup"	/* XXX - This should be gotten from
					 * the password info and the config
					 * file. It should also be an
					 * absolute pathname.
					 */

/* ColdPalm
 * Information about the Palm being currently synced.
 */
struct ColdPalm
{
	/* XXX - System information */
	/* XXX - User information */
	/* XXX - NetSync information */

	/* Memory information */
	int num_cards;			/* # memory cards */
	struct dlp_cardinfo *cardinfo;	/* Info about each memory card */

	/* Database information */
	/* XXX - There should probably be one array of these per memory
	 * card.
	 */
	int num_dbs;			/* # of databases */
	struct dlp_dbinfo *dblist;	/* Database list */
};

extern int Cold_Connect(struct PConnection *pconn, const char *name);
extern int Cold_Disconnect(struct PConnection *pconn, const ubyte status);
extern int Cold_GetMemInfo(struct PConnection *pconn, struct ColdPalm *palm);
extern int Cold_ListDBs(struct PConnection *pconn, struct ColdPalm *palm);
extern int Cold_HandleDB(struct PConnection *pconn, struct ColdPalm *palm,
			 const int dbnum);
extern int Cold_BackupDB(struct PConnection *pconn, struct ColdPalm *palm,
			 struct dlp_dbinfo *dbinfo, const char *fname);
extern int Cold_SyncDB(struct PConnection *pconn, struct ColdPalm *palm,
		       const int dbnum);
extern int Cold_RecordBackup(struct PConnection *pconn,
			     struct ColdPalm *palm,
			     struct dlp_dbinfo *dbinfo,
			     char *bakfname);

#endif	/* _coldsync_h_ */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
