/* coldsync.h
 *
 * Data structures and such needed by 'coldsync'.
 *
 * $Id: coldsync.h,v 1.4 1999-03-11 03:40:14 arensb Exp $
 */
#ifndef _coldsync_h_
#define _coldsync_h_

#include "pconn/PConnection.h"
#include "pconn/dlp_cmd.h"

#define INSTALL_DIR	"palm_install"	/* XXX - This should be gotten from
					 * the password info and the config
					 * file. It should also be an
					 * absolute pathname.
					 */

/* Palm
 * Information about the Palm being currently synced.
 */
struct Palm
{
	struct dlp_sysinfo sysinfo;	/* System information */
	struct dlp_userinfo userinfo;	/* User information */
	struct dlp_netsyncinfo netsyncinfo;
					/* NetSync information */

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
extern int Cold_GetMemInfo(struct PConnection *pconn, struct Palm *palm);
extern int Cold_ListDBs(struct PConnection *pconn, struct Palm *palm);
extern int Cold_HandleDB(struct PConnection *pconn, struct Palm *palm,
			 const int dbnum);
extern int Cold_BackupDB(struct PConnection *pconn, struct Palm *palm,
			 struct dlp_dbinfo *dbinfo, const char *fname);
extern int Cold_SyncDB(struct PConnection *pconn, struct Palm *palm,
		       const int dbnum);
extern int Cold_Backup(struct PConnection *pconn,
		       struct Palm *palm,
		       struct dlp_dbinfo *dbinfo,
		       char *bakfname);
extern int Cold_RecordSync(struct PConnection *pconn,
			   struct Palm *palm,
			   struct dlp_dbinfo *dbinfo,
			   char *bakfname);
struct pdb *LoadDatabase(char *fname);
extern int SlowSync(struct PConnection *pconn,
		    struct dlp_dbinfo *remotedb,
		    struct pdb *localdb,
		    char *bakfname);
extern struct pdb *DownloadRecordDB(struct PConnection *pconn,
				    struct dlp_dbinfo *dbinfo);

#endif	/* _coldsync_h_ */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
