/* coldsync.h
 *
 * Data structures and such needed by 'coldsync'.
 *
 * $Id: coldsync.h,v 1.8 1999-06-24 02:51:52 arensb Exp $
 */
#ifndef _coldsync_h_
#define _coldsync_h_

#include <unistd.h>		/* For uid_t */
#include <sys/types.h>		/* For uid_t */
#include <sys/param.h>		/* For MAXPATHLEN */

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

extern int need_slow_sync;
extern udword hostid;		/* This host's ID */
				/* XXX - This shouldn't be global */
extern uid_t user_uid;		/* Owner's UID */
extern char user_fullname[DLPCMD_USERNAME_LEN];
				/* Owner's full name */

/* Configuration variables */
/* XXX - There's probably a better place to put them */
extern char palmdir[MAXPATHLEN+1];	/* ~/.palm pathname */
extern char backupdir[MAXPATHLEN+1];	/* ~/.palm/backup pathname */
extern char archivedir[MAXPATHLEN+1];	/* ~/.palm/archive pathname */
extern char installdir[MAXPATHLEN+1];	/* ~/.palm/install pathname */

/* Function prototypes */
extern int Connect(struct PConnection *pconn, const char *name);
extern int Disconnect(struct PConnection *pconn, const ubyte status);
extern int GetMemInfo(struct PConnection *pconn, struct Palm *palm);
extern int ListDBs(struct PConnection *pconn, struct Palm *palm);
extern int HandleDB(struct PConnection *pconn, struct Palm *palm,
		    const int dbnum);
extern int BackupDB(struct PConnection *pconn, struct Palm *palm,
		    struct dlp_dbinfo *dbinfo, const char *fname);
extern int SyncDB(struct PConnection *pconn, struct Palm *palm,
		  const int dbnum);
extern int Backup(struct PConnection *pconn,
		  struct Palm *palm,
		  struct dlp_dbinfo *dbinfo,
		  char *bakfname);
extern int RecordSync(struct PConnection *pconn,
		      struct Palm *palm,
		      struct dlp_dbinfo *dbinfo,
		      char *bakfname);
struct pdb *LoadDatabase(char *fname);
extern int SlowSync(struct PConnection *pconn,
		    struct dlp_dbinfo *remotedb,
		    struct pdb *localdb,
		    char *bakfname);
extern int FastSync(struct PConnection *pconn,
		    struct dlp_dbinfo *remotedb,
		    struct pdb *localdb,
		    char *bakfname);
extern struct pdb *DownloadRecordDB(struct PConnection *pconn,
				    struct dlp_dbinfo *dbinfo);
extern const char *mkbakfname(const struct dlp_dbinfo *dbinfo);

#endif	/* _coldsync_h_ */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
