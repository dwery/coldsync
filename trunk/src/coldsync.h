/* coldsync.h
 *
 * Data structures and such needed by 'coldsync'.
 *
 * $Id: coldsync.h,v 1.2 1999-07-12 09:37:29 arensb Exp $
 */
#ifndef _coldsync_h_
#define _coldsync_h_

#include <unistd.h>		/* For uid_t */
#include <sys/types.h>		/* For uid_t */
#include <sys/param.h>		/* For MAXPATHLEN */

#include "PConnection.h"
#include "dlp_cmd.h"

#define CARD0	0		/* Memory card #0. The only real purpose of
				 * this is to make it marginally easier to
				 * find all of the places where card #0 has
				 * been hardcoded, once support for
				 * multiple memory cards is added, if it
				 * ever is.
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

/* cmd_opts
 * Command-line options. This nameless struct acts sort of like a C++
 * namespace, and just serves to make sure there are no name collisions.
 */
struct cmd_opts {
	char *config_file;	/* Pathname of configuration file */
	Bool do_backup;		/* True iff we should do a full backup */
	char *backupdir;	/* Where to put the files when doing a 
				 * backup */
	Bool do_restore;	/* True iff we should restore from a
				 * full backup */
	char *restoredir;	/* Where to restore from */
	Bool force_slow;	/* If true, force slow syncing */
	Bool force_fast;	/* If true, force fast syncing */

	char *port;		/* Serial port to sync on */
				/* XXX - It should be possible to listen on
				 * multiple ports at once, at which point
				 * this will have to become an array or
				 * linked list or something.
				 */
	char *username;		/* Name of user to run as */
	uid_t uid;		/* UID of user to run as */
	Bool check_ROM;		/* Iff false, ignore ROM databases */
	Bool install_first;	/* If true, install new databases before
				 * doing the rest of the sync. Otherwise,
				 * install them after everything else has
				 * been synced.
				 */
};

extern struct cmd_opts global_opts;

/* debug_flags
 * Debugging levels. This nameless struct acts sort of like a C++
 * namespace, and just serves as a convenient place to put all of the
 * debugging level varialbes.
 */
struct debug_flags {
	int slp;		/* Serial Link Protocol */
	int cmp;		/* Connection Management Protocol */
	int padp;		/* Packet Assembly/Disassembly Protocol */
	int dlp;		/* Data Link Protocol */
	int dlpc;		/* DLP commands */
	int sync;		/* General synchoronization */
	int misc;		/* Anything that doesn't readily fall into
				 * the above categories.
				 */
};

extern struct debug_flags debug;

#define TRACE(cat,level)	if (debug.cat >= (level))
#define SLP_TRACE(level)	TRACE(slp,(level))
#define CMP_TRACE(level)	TRACE(cmp,(level))
#define PADP_TRACE(level)	TRACE(padp,(level))
#define DLP_TRACE(level)	TRACE(dlp,(level))
#define DLPC_TRACE(level)	TRACE(dlpc,(level))
#define SYNC_TRACE(level)	TRACE(sync,(level))
#define MISC_TRACE(level)	TRACE(misc,(level))

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
/*extern int Backup(struct PConnection *pconn,
		  struct Palm *palm,
		  struct dlp_dbinfo *dbinfo,
		  char *bakfname);*/
extern int Backup(struct PConnection *pconn,
		  struct Palm *palm);
extern const char *mkbakfname(const struct dlp_dbinfo *dbinfo);

#endif	/* _coldsync_h_ */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
