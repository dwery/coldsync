/* coldsync.h
 *
 * Data structures and such needed by 'coldsync'.
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: coldsync.h,v 1.8 1999-11-04 11:40:09 arensb Exp $
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
/* XXX - Should this be an enum? */
#define LISTEN_NONE	0	/* Dunno if this will be useful */
#define LISTEN_SERIAL	1	/* Listen on serial port */
#define LISTEN_TCP	2	/* Listen on TCP port (not
				 * implemented yet). */

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
				 * constants below.
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
	char *path;		/* Path to conduit */
} conduit_block;

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

/* XXX - Should there be a separate struct to hold the user configuration,
 * i.e., the user's name, uid, directories where to put all the various
 * stuff etc.? Or maybe for a particular invocation, i.e., when we've
 * already figured out which user and which Palm we're dealing with?
 */

/* conduit_spec
 * Describes a conduit.
 */
struct conduit_spec
{
	char name[COND_NAMELEN];	/* Name of this conduit */

	/* What databases does this conduit apply to? */
	char dbname[DLPCMD_DBNAME_LEN];
			/* Name of the database that the conduit
			 * applies to. Empty string means conduit
			 * applies to all databases */
	udword dbtype;	/* Database type. 0 means conduit applies to
			 * all types. */
	udword dbcreator;
			/* Database creator. 0 means conduit applies
			 * to all creators. */
	/* XXX - Need an enum to specify whether this is a regular
	 * conduit, a pre-fetch function, or a post-dump function.
	 */

	/* The actual functions involved */
	/* XXX - Should there be some mechanism to indicate that this
	 * conduit may be used in conjunction with others? I.e., there
	 * might be one post-dump function that reads the address book
	 * and converts it to a MH mail alias list, another that
	 * converts it to a Pine alias list. Both should be run.
	 */
	ConduitFunc run;
			/* The conduit function. */
};

/* conduit_config
 * Configuration for a given conduit: specifies when it should be run
 * (which databases and whatnot).
 */
struct conduit_config
{
	/* Database characteristics
	 * If 'dbname' is the empty string, it's a wildcard: this
	 * conduit is applicable to all databases. If 'dbtype' or
	 * 'dbcreator' is 0, then it's a wildcard: this conduit
	 * applies to all types and creators.
	 * When a conduit is registered, 'dbname', 'dbtype' and
	 * 'dbcreator' are and-ed with their corresponding values in
	 * '*conduit'. Thus, if a conduit applies to (*, 'appl', *)
	 * and is registered with ("Memo Pad", *, *), then it will
	 * only be invoked for ("Memo Pad", 'appl', *).
	 */
	char dbname[DLPCMD_DBNAME_LEN];	/* Database name */
	udword dbtype;			/* Database type */
	udword dbcreator;		/* Database creator */

	/* Other information */
	Bool mandatory;			/* If true, stop looking if
					 * this conduit matches:
					 * prevents other conduits
					 * from overriding this one.
					 */

	/* Information needed at run-time */
	struct conduit_spec *conduit;	/* The conduit itself. If this
					 * is NULL, then don't do
					 * anything when syncing this
					 * database.
					 */
	/* XXX - Arguments and such (e.g., for commands or scripts) */
};

/* XXX - conduit_config2
 * This struct should replace 'conduit_config' eventually.
 */
struct conduit_config2
{
	struct conduit_config2 *next;	/* Next descriptor on the list */

	char pathname[MAXPATHLEN];	/* Path to conduit program */
	conduit_flavor flavor;		/* What flavor of conduit is this? */

	/* Which databases does this conduit apply to? */
	char dbname[DLPCMD_DBNAME_LEN];	/* Database name */
	udword dbtype;			/* Database type */
	udword dbcreator;		/* Database creator */

	Bool final;			/* If this conduit applies, don't
					 * look any further. */
	Bool isdefault;			/* Use this conduit iff no other
					 * applies. */

	/* XXX - Conduit arguments and whatnot */
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
extern char atticdir[MAXPATHLEN+1];	/* ~/.palm/backup/Attic pathname */
extern char archivedir[MAXPATHLEN+1];	/* ~/.palm/archive pathname */
extern char installdir[MAXPATHLEN+1];	/* ~/.palm/install pathname */

extern struct config config;		/* Main configuration */

/* Function prototypes */
extern struct config *new_config();
extern void free_config(struct config *config);
extern int parse_config(const char *fname, struct config *config);
extern listen_block *new_listen_block();
extern void free_listen_block(listen_block *l);
extern int Connect(struct PConnection *pconn, const char *name);
extern int Disconnect(struct PConnection *pconn, const ubyte status);
extern int GetMemInfo(struct PConnection *pconn, struct Palm *palm);
extern int ListDBs(struct PConnection *pconn, struct Palm *palm);
extern int HandleDB(struct PConnection *pconn, struct Palm *palm,
		    const int dbnum);
extern int Backup(struct PConnection *pconn,
		  struct Palm *palm);
extern int Restore(struct PConnection *pconn,
		  struct Palm *palm);
extern int InstallNewFiles(struct PConnection *pconn,
			   struct Palm *palm,
			   char *newdir,
			   Bool deletep);
extern void usage(int argc, char *argv[]);
extern int parse_args(int argc, char *argv[]);
extern void print_version(void);
extern void set_debug_level(const char *str);
extern int load_config();
extern const char *mkbakfname(const struct dlp_dbinfo *dbinfo);
extern struct dlp_dbinfo *find_dbentry(struct Palm *palm,
				       const char *name);
extern int append_dbentry(struct Palm *palm,
			  struct pdb *pdb);
extern int get_config(int argc, char *argv[]);


#endif	/* _coldsync_h_ */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
