/* coldsync.h
 *
 * Data structures and such needed by 'coldsync'.
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: coldsync.h,v 1.71 2002-11-02 12:51:18 azummo Exp $
 */
#ifndef _coldsync_h_
#define _coldsync_h_

#include "config.h"
#include <unistd.h>		/* For uid_t */
#include <sys/types.h>		/* For uid_t */
#include <sys/param.h>		/* For MAXPATHLEN */
#include "pconn/pconn.h"
#include "pdb.h"
#include "spalm.h"
#include "trace.h"

#define COND_NAMELEN		128	/* Max. length of conduit name */
#define DEFAULT_GLOBAL_CONFIG	SYSCONFDIR "/coldsync.conf"

#define GLOBAL_INSTALL_DIR	"/usr/share/coldsync/install"

extern int sync_trace;		/* Debugging level for sync-related stuff */
extern int misc_trace;		/* Debugging level for miscellaneous stuff */
extern int conduit_trace;	/* Debugging level for conduits-related stuff */


/* Bool3
 * This is just like a boolean, except that it also includes the value
 * "Undefined", meaning that the value hasn't been set. This is useful for
 * variables.
 */
typedef enum { False3 = 0, True3 = 1, Undefined } Bool3;

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
	mode_Daemon,		/* Daemon mode: runs as root. Other options
				 * determine whether ColdSync is run from
				 * inetd, getty, or what.
				 */
				/* XXX - Still highly experimental. */
	mode_Init		/* Initialize a Palm */
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
	pconn_listen_t devtype;	/* Type of device (serial, USB, TCP, etc.) */
	pconn_proto_t protocol;	/* Protocol stack for talking to cradle */
	Bool use_syslog;	/* Use syslog for error messages? */
	char *log_fname;	/* Where to write the log file */
	Bool force_slow;	/* If true, force slow syncing */
	Bool force_fast;	/* If true, force fast syncing */
	Bool check_ROM;		/* If false, ignore ROM databases */
	Bool3 autoinit;		/* If true, tries to initialize the palm 
				 * when in daemon mode.
				 */
	Bool3 install_first;	/* If true, install new databases before
				 * doing the rest of the sync. Otherwise,
				 * install them after everything else has
				 * been synced.
				 */
	Bool3 force_install;	/* If true, install databases from
				 * ~/.palm/install, even if the modnum of
				 * the database to be installed is smaller
				 * than that of the existing database.
				 */
	int verbosity;		/* Level of verbosity. This is different
				 * from debugging messages, in that
				 * debugging messages are purely for the
				 * developer's sake; these are for the
				 * curious user's sake. Hence, these
				 * messages should be translated, for
				 * instance.
				 */
	char *listen_name;	/* Requested listen block name
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
	pconn_listen_t listen_type;
				/* Block type. See the LISTEN_*
				 * constants in "pconn/PConnection.h"
				 */
	pconn_proto_t protocol;	/* Protocol stack. See the PCONN_STACK_*
				 * constants in "pconn/PConnection.h"
				 */
	char *device;		/* Device to listen on */
	long speed;
	unsigned short flags;	/* Flags. See LISTENFL_*, below */
	char *name;		/* Name of this listen block. */
} listen_block;

#define LISTENFL_TRANSIENT	0x01	/* This device is transient: it
					 * might not exist at any given
					 * time (e.g., if it's on a devfs
					 * filesystem), so an open() that
					 * fails with ENOENT isn't an
					 * error.
					 */
#define LISTENFL_PROMPT		0x02	/* Prompt for the HotSync button */
#define LISTENFL_NOCHANGESPEED	0x04	/* This device is a modem */

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
	unsigned char flags;		/* Flags. See CREATYPEFL_* below */
} crea_type_t;

/* crea_type_t flags:
 */

#define CREATYPEFL_ISNONE (1<<0)	/* "none" was given as creator/type pair */


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
	char *cwd;		/* Conduit working directory */
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
#define FLAVORFL_INIT		(1 << 4)

/* pda_block
 * The information specified in a 'pda' block in the config file: the
 * serial number, base directory, and so forth.
 */
typedef struct pda_block
{
	struct pda_block *next;
	unsigned char flags;		/* PDAFL_* flags */
	char *name;			/* Name of this PDA */
	char *snum;			/* Serial number */
	char *directory;		/* Base directory, where the
					 * "backup", "archive" etc.
					 * directories will be created.
					 */
	char *username;			/* Owner's full name */
	Bool userid_given;		/* Was a user ID specified? */
	udword userid;			/* Owner's user ID */

	Bool forward;			/* Should we consider forwarding
					 * this PDA's data to another host? */
	char *forward_host;		/* Host to which to forward the
					 * connection. */
	char *forward_name;		/* Name to give in the NetSync
					 * wakeup packet. */
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
	struct {
		Bool3 force_install;	/* Install databases in
					 * ~/.palm/install, even if they're
					 * the same version as on the Palm.
					 */
		Bool3 install_first;	/* Install new databases before the
					 * main sync.
					 */
		Bool3 autoinit;		/* If true, tries to initialize the palm 
					 * when in daemon mode
					 */
		Bool3 autorescue;	/* If true, tries to upload the missing files
					 * from the rescue directory 
					 * when in daemon mode
					 */
		Bool3 filter_dbs;	/* If true, coldsync will retrieve from the pda
					 * only the dbs specified in the conduit blocks.
					 */
		/* XXX - Perhaps allow "final" here, so that the sysadmin
		 * can lock options in place.
		 */
	} options;
	listen_block *listen;		/* List of listen blocks */
	pda_block *pda;			/* List of known PDAs */
	conduit_block *conduits;	/* List of all conduits */
};

/* XXX - A lot of these variables need to be rethought */
extern int need_slow_sync;
extern udword hostid;			/* This host's ID */
					/* XXX - This shouldn't be global */
extern struct sockaddr **hostaddrs;
extern int num_hostaddrs;
extern struct userinfo userinfo;	/* Information about the (Unix)
					 * user */

/* Configuration variables */
/* XXX - There's probably a better place to put them. 'sync_config',
 * perhaps?
 */
extern char palmdir[MAXPATHLEN+1];	/* ~/.palm pathname */
extern char backupdir[MAXPATHLEN+1];	/* ~/.palm/backup pathname */
extern char atticdir[MAXPATHLEN+1];	/* ~/.palm/backup/Attic pathname */
extern char archivedir[MAXPATHLEN+1];	/* ~/.palm/archive pathname */
extern char installdir[MAXPATHLEN+1];	/* ~/.palm/install pathname */
extern char rescuedir[MAXPATHLEN+1];	/* ~/.palm/rescue pathname */

extern struct sync_config *sync_config;

/* Function prototypes */
/* coldsync.c */
extern int dbinfo_fill(struct dlp_dbinfo *dbinfo,
		       struct pdb *pdb);
extern char snum_checksum(const char *snum, int len);
extern int open_tempfile(char *name_template);

/* config.c */
extern int parse_args(int argc, char *argv[]);
extern int load_config(const Bool read_user_config);
extern void print_version(FILE *outfile);
extern int get_hostinfo(void);
extern int get_hostaddrs(void);
extern void free_hostaddrs(void);
extern void print_pda_block(FILE *outfile,
			    const pda_block *pda,
			    struct Palm *palm);
extern pda_block *find_pda_block(struct Palm *palm,
				 const Bool check_user);
extern listen_block * find_listen_block( char *name );

extern int make_sync_dirs(const char *basedir);
extern struct sync_config *new_sync_config(void);
extern void free_sync_config(struct sync_config *config);
extern listen_block *new_listen_block(void);
extern void free_listen_block(listen_block *l);
extern int prepend_listen_block(char *devname, pconn_listen_t listen_type, pconn_proto_t protocol);
extern pconn_listen_t name2listen_type(const char *str);
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
			    const udword type,
			    const unsigned char flags);

/* backup.c */
extern struct pdb * download_database(
	PConnection *pconn,
	const struct dlp_dbinfo *dbinfo,
	ubyte dbh);
extern int backup(PConnection *pconn,
		  const struct dlp_dbinfo *dbinfo,
		  const char *dirname);
extern int full_backup(PConnection *pconn,
		       struct Palm *palm,
		       const char *backupdir);

/* restore.c */
extern int restore_file(PConnection *pconn,
			struct Palm *palm,
			const char *fname);
extern int restore_dir(PConnection *pconn,
		       struct Palm *palm,
		       const char *dirname);

/* install.c */
extern int upload_database(PConnection *pconn, struct pdb *db);
extern int NextInstallFile(struct dlp_dbinfo *dbinfo);
extern int InstallNewFiles(struct Palm *palm,
			   char *newdir,
			   Bool deletep,
			   Bool force_install);

/* log.c */
extern int va_add_to_log(PConnection *pconn, const char *fmt, ...);

/* parser.y */
extern int parse_config_file(const char *fname, struct sync_config *config);

/* misc.c */
extern int Warn(const char *format, ...);
extern int Error(const char *format, ...);
extern void Perror(const char *str);
extern int Verbose(const int level, const char *str, ...);
extern const char *mkfname(const char *first, ...);
extern const char *mkpdbname(const char *dirname,
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
extern const char *Bool3str(const Bool3 var);

#if !HAVE_SNPRINTF
extern int snprintf(char *buf, size_t len, const char *format, ...);
#endif	/* HAVE_SNPRINTF */

#if !HAVE_VSNPRINTF
extern int vsnprintf(char *buf, size_t len, const char *format, ...);
#endif	/* HAVE_SNPRINTF */

#endif	/* _coldsync_h_ */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
