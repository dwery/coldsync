/* coldsync.h
 *
 * Data structures and such needed by 'coldsync'.
 *
 *	Copyright (C) 1999, 2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: coldsync.h,v 1.37 2000-11-18 23:55:28 arensb Exp $
 */
#ifndef _coldsync_h_
#define _coldsync_h_

#include "config.h"
#include <unistd.h>		/* For uid_t */
#include <sys/types.h>		/* For uid_t */
#include <sys/param.h>		/* For MAXPATHLEN */
#include "pconn/pconn.h"
#include "pdb.h"
#include "palm.h"

#define COND_NAMELEN		128	/* Max. length of conduit name */
#define DEFAULT_GLOBAL_CONFIG	SYSCONFDIR "/coldsync.conf"

extern int sync_trace;		/* Debugging level for sync-related stuff */
extern int misc_trace;		/* Debugging level for miscellaneous stuff */

#define SYNC_TRACE(n)	if (sync_trace >= (n))
#define MISC_TRACE(n)	if (misc_trace >= (n))

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

/* run_mode
 * Lists the various overall modes.
 */
typedef enum {
	mode_None = 0,		/* No mode selected yet */ 
	mode_Standalone,	/* Single-shot mode: sync once, then exit */
	mode_Backup,		/* Back up a Palm */
	mode_Restore,		/* Restore from backups */
	mode_Install,		/* Install only */
	mode_Daemon,		/* Daemon mode: run continuously in the
				 * background, waiting for connections and
				 * forking as necessary.
				 */
				/* XXX - Not implemented yet */
	mode_Getty,		/* getty mode: the Palm is on stdin */
				/* XXX - Not implemented yet */
	mode_Init		/* Initialize a Palm */
				/* XXX - Not implemented yet */
} run_mode;

/* cmd_opts Command-line options. This struct acts sort of like a C++
 * namespace, and just serves to make sure there are no name collisions.
 */
struct cmd_opts {
	run_mode mode;		/* Mode in which to run */
	char *conf_fname;	/* User configuration file name */
	Bool conf_fname_given;	/* If true, user specified a config file on
				 * the command line.
				 */
	char *devname;		/* Name of the device on which to listen */
	int devtype;		/* Type of device (serial, USB, TCP, etc.) */
	/* XXX - do_backup and do_restore are made obsolete by run modes */
	/* XXX - backupdir and restoredir are made obsolete by
	 * mode-specific functions.
	 */
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
	long speed;
} listen_block;

/* cond_header
 * A (name, value) pair that will be passed to a conduit.
 */
struct cond_header {
	struct cond_header *next;	/* Next header on the list */
	char *name;
	char *value;
};

/* pref_desc
 * Describes a preference (i.e., a resource in "Saved Preferences.prc" or
 * "Unsaved Preferences.prc"): its creator, numeric identifier, and whether
 * it is a saved or unsaved preference.
 */
struct pref_desc {
	udword creator;			/* 4-char creator */
	uword id;			/* Numeric identifier */
	char flags;			/* Flags. See PREFDFL_*, below */
};

/* crea_type_t
 * A convenience type for representing the creator/type pair that owns a
 * database.
 */
typedef struct crea_type_t {
	udword creator;			/* 4-character creator */
	udword type;			/* 4-character type */
} crea_type_t;

/* pref_desc flags:
 * If neither flag is set, then the location of the preference is
 * unspecified; look in both "Saved Preferences" and "Unsaved Preferences".
 */
#define PREFDFL_SAVED	(1<<0)		/* Look for this preference in the
					 * saved preferences.
					 */
#define PREFDFL_UNSAVED	(1<<1)		/* Look in the unsaved preferences */

/* conduit_block
 * The information specified in a 'conduit' block: its type, pathname,
 * arguments, and so forth.
 *
 * Conduits come in several flavors:
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

 * - 'Install' (or pre-install) runs before a database is uploaded to the
 *   Palm. It may modify its input, upload records manually (it runs while
 *   a connection is active), or whatever it likes. Anything left in
 *   ~/.palm/install after the Install conduits have run gets uploaded.
 */
typedef struct conduit_block
{
	struct conduit_block *next;
	unsigned short flavors;	/* Bitmap of flavors that this conduit
				 * implements. See FLAVORFL_*, below.
				 */

	crea_type_t *ctypes;	/* Which creator/types the conduit applies
				 * to. */
	int ctypes_slots;	/* Size of the 'ctypes' array (how many
				 * 'crea_type_t's will fit in it?
				 */
	int num_ctypes;		/* # of entries in the 'ctypes' array */

	unsigned char flags;	/* CONDFL_* flags */
	char *path;		/* Path to conduit */
	struct cond_header *headers;	/* User-supplied headers */

	struct pref_desc *prefs;	/* Array of preferences that this
					 * conduit cares about. */
	int prefs_slots;	/* Size of the 'prefs' array (how many
				 * 'struct pref_desc's will fit in it?
				 */
	int num_prefs;		/* # of entries in the 'prefs' array */
} conduit_block;

#define CONDFL_DEFAULT	0x01	/* This is a default conduit: if no other
				 * conduit matches, use this one.
				 */
#define CONDFL_FINAL	0x02	/* If this conduit matches, don't run any
				 * other conduits for this database.
				 */
/* Conduit flavor flags. These are really bitmasks for
 * conduit_block.flavors.
 */
#define FLAVORFL_FETCH		(1 << 0)
#define FLAVORFL_DUMP		(1 << 1)
#define FLAVORFL_SYNC		(1 << 2)
#define FLAVORFL_INSTALL	(1 << 3)

/* pda_block
 * The information specified in a 'pda' block in the config file: the
 * serial number, base directory, and so forth.
 */
typedef struct pda_block
{
	struct pda_block *next;
	char *name;			/* Name of this PDA */
	char *snum;			/* Serial number */
	char *directory;		/* Base directory, where the
					 * "backup", "archive" etc.
					 * directories will be created.
					 */
	char *username;			/* Owner's full name */
	udword userid;			/* Owner's user ID */
	unsigned char flags;		/* PDAFL_* flags */
	/* XXX - List of preferences that the conduit is interested in */
} pda_block;

#define PDAFL_DEFAULT	0x01		/* This is the default PDA
					 * configuration. It is used if no
					 * other pda_block matches.
					 */

/* sync_config
 * Holds everything that needs to be known once we're ready to synchronize
 * a particular Palm, belonging to a particular user. This will hold a lot
 * of the global variables, below.
 */
struct sync_config {
	/* XXX */
	listen_block *listen;		/* List of listen blocks */
	pda_block *pda;			/* List of known PDAs */
	conduit_block *conduits;	/* List of all conduits */
};

/* XXX - A lot of these variables need to be rethought */
extern int need_slow_sync;
extern udword hostid;			/* This host's ID */
					/* XXX - This shouldn't be global */
extern uid_t user_uid;			/* Owner's UID */
extern char user_fullname[DLPCMD_USERNAME_LEN];
					/* Owner's full name */

/* Configuration variables */
/* XXX - There's probably a better place to put them. 'sync_config',
 * perhaps?
 */
extern char palmdir[MAXPATHLEN+1];	/* ~/.palm pathname */
extern char backupdir[MAXPATHLEN+1];	/* ~/.palm/backup pathname */
extern char atticdir[MAXPATHLEN+1];	/* ~/.palm/backup/Attic pathname */
extern char archivedir[MAXPATHLEN+1];	/* ~/.palm/archive pathname */
extern char installdir[MAXPATHLEN+1];	/* ~/.palm/install pathname */

extern struct sync_config *sync_config;

/* Function prototypes */
extern struct sync_config *new_sync_config(void);
extern void free_sync_config(struct sync_config *config);
extern int parse_config_file(const char *fname, struct sync_config *config);
extern listen_block *new_listen_block(void);
extern void free_listen_block(listen_block *l);
extern conduit_block *new_conduit_block(void);
extern void free_conduit_block(conduit_block *c);
extern pda_block *new_pda_block(void);
extern void free_pda_block(pda_block *p);
extern int append_pref_desc(conduit_block *cond,
			    const udword creator,
			    const uword id,
			    const char flags);
extern int append_crea_type(conduit_block *cond,
			    const udword creator,
			    const udword type);
extern int Connect(struct PConnection *pconn);
extern int Disconnect(struct PConnection *pconn, const ubyte status);
extern int run_mode_Standalone(int argc, char *argv[]);
extern int run_mode_Backup(int argc, char *argv[]);
extern int run_mode_Restore(int argc, char *argv[]);
/*  extern int run_mode_Install(int argc, char *argv[]); */
extern int backup(struct PConnection *pconn,
		  const struct dlp_dbinfo *dbinfo,
		  const char *dirname);
extern int full_backup(struct PConnection *pconn,
		       struct Palm *palm,
		       const char *backupdir);
extern int restore_file(struct PConnection *pconn,
			struct Palm *palm,
			const char *fname);
extern int restore_dir(struct PConnection *pconn,
		       struct Palm *palm,
		       const char *dirname);
extern int NextInstallFile(struct dlp_dbinfo *dbinfo);
extern int InstallNewFiles(struct PConnection *pconn,
			   struct Palm *palm,
			   char *newdir,
			   Bool deletep);
extern void usage(int argc, char *argv[]);
extern void print_version(void);
extern void set_debug_level(const char *str);
extern int set_mode(const char *str);
extern const char *mkfname(const char *dirname,
			   const struct dlp_dbinfo *dbinfo,
			   Bool add_suffix);
extern const char *mkbakfname(const struct dlp_dbinfo *dbinfo);
extern const char *mkinstfname(const struct dlp_dbinfo *dbinfo);
extern const char *mkarchfname(const struct dlp_dbinfo *dbinfo);
extern const char *fname2dbname(const char *fname);
extern const Bool exists(const char *fname);
extern const Bool lexists(const char *fname);
extern const Bool is_file(const char *fname);
extern const Bool is_directory(const char *fname);
extern const Bool is_database_name(const char *fname);
extern int dbinfo_fill(struct dlp_dbinfo *dbinfo,
		       struct pdb *pdb);
extern char snum_checksum(const char *snum, int len);
extern int open_tempfile(char *name_template);
extern int load_config(void);
extern int parse_args(int argc, char *argv[]);
extern int add_to_log(const char *msg);

#endif	/* _coldsync_h_ */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
