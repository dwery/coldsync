/* coldsync.h
 *
 * Data structures and such needed by 'coldsync'.
 *
 *	Copyright (C) 1999, 2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: coldsync.h,v 1.18 2000-03-14 06:17:32 arensb Exp $
 */
#ifndef _coldsync_h_
#define _coldsync_h_

#include "config.h"
#include <unistd.h>		/* For uid_t */
#include <sys/types.h>		/* For uid_t */
#include <sys/param.h>		/* For MAXPATHLEN */
#include "pconn/pconn.h"
#include "pdb.h"

#define COND_NAMELEN		128	/* Max. length of conduit name */
#define DEFAULT_GLOBAL_CONFIG	SYSCONFDIR "/coldsync.rc"

extern int sync_trace;		/* Debugging level for sync-related stuff */
extern int misc_trace;		/* Debugging level for miscellaneous stuff */

#define SYNC_TRACE(n)	if (sync_trace >= (n))
#define MISC_TRACE(n)	if (misc_trace >= (n))

/* Types of listen blocks */
/* XXX - This should go elsewhere, in PConnection.h or something */
#define LISTEN_NONE	0	/* Dunno if this will be useful */
#define LISTEN_SERIAL	1	/* Listen on serial port */
#define LISTEN_TCP	2	/* Listen on TCP port (not
				 * implemented yet). */
#define LISTEN_USB	3	/* USB for Handspring Visor */

typedef int comm_type;

/* userinfo
 * Information about the user whose Palm we're syncing.
 */
struct userinfo
{
	uid_t uid;
	char fullname[DLPCMD_USERNAME_LEN];
					/* User's full name */
	char homedir[MAXPATHLEN];	/* Home directory */
};

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
	 * card. Or perhaps 'struct dlp_dbinfo' should say which memory
	 * card it is on.
	 */
	int num_dbs;			/* # of databases */
	struct dlp_dbinfo *dblist;	/* Database list */
};

/* XXX - ConduitFunc() should disappear once the conduit API has been
 * rebuilt.
 */
typedef int (*ConduitFunc)(struct PConnection *pconn,
			   struct Palm *palm,
			   struct dlp_dbinfo *db);

typedef enum { Standalone, Daemon } run_mode;
				/* XXX - Daemon mode hasn't been
				 * implemented yet.
				 */
				/* XXX - Ought to have a better name than
				 * 'Standalone'.
				 */

/* XXX - Configuration
 * There should be three config files:
 *
 * In standalone mode, ~/.coldsyncrc contains the user's listen and conduit
 * blocks. /etc/coldsync.common (?) contains the system-wide defaults
 * (e.g., the list of listen blocks for this machine, the default conduit
 * setup, etc.).
 *
 * In daemon mode, /etc/coldsync.common contains the system-wide defaults,
 * as above. /etc/coldsync.conf contains the specific configuration for
 * running in daemon mode.
 */

/* cmd_opts
 * Command-line options. This nameless struct acts sort of like a C++
 * namespace, and just serves to make sure there are no name collisions.
 */
struct cmd_opts {
	run_mode mode;		/* How to run: standalone or daemon */
	Bool do_backup;		/* True iff we should do a full backup */
	char *backupdir;	/* Where to put the files when doing a 
				 * backup */
	Bool do_restore;	/* True iff we should restore from a
				 * full backup */
	char *restoredir;	/* Where to restore from */
	Bool force_slow;	/* If true, force slow syncing */
	Bool force_fast;	/* If true, force fast syncing */
	Bool check_ROM;		/* Iff false, ignore ROM databases */
	Bool install_first;	/* If true, install new databases before
				 * doing the rest of the sync. Otherwise,
				 * install them after everything else has
				 * been synced.
				 */
	Bool force_install;	/* If true, install databases from
				 * ~/.palm/install, even if the modnum of
				 * the database to be installed is smaller
				 * than that of the existing database.
				 */
};

extern struct cmd_opts global_opts;	/* XXX - I'm not quite happy with
					 * this global variable.
					 */

/* listen_block
 * The information specified in a 'listen' block: which device to
 * listen on, and so forth.
 */
typedef struct listen_block
{
	struct listen_block *next;
	int listen_type;	/* Block type. See the LISTEN_*
				 * constants above.
				 */
	char *device;		/* Device to listen on */
	int speed;		/* How fast to sync */
} listen_block;

/* conduit_flavor
 * Conduits come in five flavors:
 * - 'Sync' is the conduit we all know and love: it has an open connection
 *   to the Palm that it can use to read the contents of a database, upload
 *   and download records, and generally make sure that whatever's on the
 *   disk is the same thing as what's on the Palm. This is the most generic
 *   flavor of conduit, and hence the hardest to implement.
 *	If all you want to do is copy data back and forth, don't use this.
 *   The only case I've discovered in which you actually want to use a
 *   'Sync' conduit is for the Mail application.
 *
 * - 'Fetch' (or pre-fetch) fetches data from wherever it likes, then
 *   creates (or appends to) a .pdb file. Some other conduit will be
 *   responsible for uploading this data to the Palm. Use this when the
 *   master copy of the data doesn't reside on the Palm, e.g., if you want
 *   to upload stock quote data to the Palm.
 *	You can also use this in conjunction with a 'Dump' conduit to
 *   synchronize data with a non-PDB file on the desktop, e.g., to keep the
 *   ToDo application in sync with a plain-text "TODO" file in your home
 *   directory.
 *
 * - 'Dump' is the converse of 'Fetch': it reads a .pdb file and writes it
 *   to wherever it likes. Some other conduit is responsible for putting
 *   the information in that .pdb file. This is useful for implementing
 *   "Handheld Overwrites Desktop" rules.
 *
 * - 'Install' is not very well fleshed out at the moment, and is not
 *   expected to be very useful in the future. The idea is that if a new
 *   application has just appeared, an Install conduit can create the
 *   corresponding dot files on your workstation. Or something.
 *
 * - 'Uninstall' is not very well fleshed out at the moment, and is not
 *   expected to be very useful in the future. The idea is if that some
 *   application has disappeared from your Palm, the Uninstall conduit can
 *   do any required cleanup. Or something.
 */
typedef enum {
	Sync,
	Fetch,
	Dump,
	Install,
	Uninstall
} conduit_flavor;

/* conduit_block
 * The information specified in a 'conduit' block: its type, pathname,
 * arguments, and so forth.
 */
typedef struct conduit_block
{
	struct conduit_block *next;
	conduit_flavor flavor;	/* What flavor of conduit is this? */
	udword dbtype;		/* What database types does it apply to? */
	udword dbcreator;
	unsigned char flags;	/* CONDFL_* flags */
	char *path;		/* Path to conduit */
} conduit_block;

#define CONDFL_DEFAULT	0x01	/* This is a default conduit: if no other
				 * conduit matches, use this one.
				 */
#define CONDFL_FINAL	0x02	/* If this conduit matches, don't run any
				 * other conduits for this database.
				 */

struct config
{
	run_mode mode;
	listen_block *listen;		/* List of listen blocks */
	conduit_block *sync_q;		/* List of sync conduits */
	conduit_block *fetch_q;		/* List of fetch conduits */
	conduit_block *dump_q;		/* List of dump conduits */
	conduit_block *install_q;	/* List of install conduits */
	conduit_block *uninstall_q;	/* List of uninstall conduits */
};

extern int sys_maxfds;			/* Max # of file descriptors
					 * allowed for this process.
					 */
extern int need_slow_sync;
extern udword hostid;			/* This host's ID */
					/* XXX - This shouldn't be global */
extern uid_t user_uid;			/* Owner's UID */
extern char user_fullname[DLPCMD_USERNAME_LEN];
					/* Owner's full name */

/* Configuration variables */
/* XXX - There's probably a better place to put them */
extern char palmdir[MAXPATHLEN+1];	/* ~/.palm pathname */
extern char backupdir[MAXPATHLEN+1];	/* ~/.palm/backup pathname */
extern char atticdir[MAXPATHLEN+1];	/* ~/.palm/backup/Attic pathname */
extern char archivedir[MAXPATHLEN+1];	/* ~/.palm/archive pathname */
extern char installdir[MAXPATHLEN+1];	/* ~/.palm/install pathname */

extern struct config config;		/* Main configuration */
extern struct userinfo userinfo;	/* Information about the user
					 * whose Palm this is */

/* Function prototypes */
extern struct config *new_config();
extern void free_config(struct config *config);
extern int parse_config(const char *fname, struct config *config);
extern listen_block *new_listen_block();
extern void free_listen_block(listen_block *l);
extern conduit_block *new_conduit_block();
extern void free_conduit_block(conduit_block *c);
extern int Connect(struct PConnection *pconn);
extern int Disconnect(struct PConnection *pconn, const ubyte status);
extern int GetMemInfo(struct PConnection *pconn, struct Palm *palm);
extern int ListDBs(struct PConnection *pconn, struct Palm *palm);
extern int HandleDB(struct PConnection *pconn, struct Palm *palm,
		    const int dbnum);
extern int backup(struct PConnection *pconn,
		  struct dlp_dbinfo *dbinfo,
		  const char *dirname);
extern int full_backup(struct PConnection *pconn,
		       struct Palm *palm);
extern int Restore(struct PConnection *pconn,
		  struct Palm *palm);
extern int InstallNewFiles(struct PConnection *pconn,
			   struct Palm *palm,
			   char *newdir,
			   Bool deletep);
extern void usage(int argc, char *argv[]);
extern void print_version(void);
extern void set_debug_level(const char *str);
extern const char *mkfname(const char *dirname,
			   const struct dlp_dbinfo *dbinfo,
			   Bool add_suffix);
extern const char *mkbakfname(const struct dlp_dbinfo *dbinfo);
extern const char *mkarchfname(const struct dlp_dbinfo *dbinfo);
extern const char *fname2dbname(const char *fname);
extern struct dlp_dbinfo *find_dbentry(struct Palm *palm,
				       const char *name);
extern int append_dbentry(struct Palm *palm,
			  struct pdb *pdb);
extern int get_config(int argc, char *argv[]);
extern int add_to_log(char *msg);

#endif	/* _coldsync_h_ */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */