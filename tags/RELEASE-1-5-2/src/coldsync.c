/* coldsync.c
 *
 *	Copyright (C) 1999, 2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: coldsync.c,v 1.57 2000-11-19 00:10:59 arensb Exp $
 */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>		/* For malloc(), atoi() */
#include <fcntl.h>		/* For open() */
#include <sys/param.h>		/* For MAXPATHLEN */
#include <termios.h>		/* Experimental */
#include <dirent.h>		/* For opendir(), readdir(), closedir() */
#include <string.h>		/* For strrchr() */

#if HAVE_STRINGS_H
#  include <strings.h>		/* For strcasecmp() under AIX */
#endif	/* HAVE_STRINGS_H */

#include <unistd.h>		/* For sleep(), getopt() */
#include <ctype.h>		/* For isalpha() and friends */
#include <errno.h>		/* For errno. Duh. */

/* Include I18N-related stuff, if necessary */
#if HAVE_LIBINTL_H
#  include <locale.h>		/* For setlocale() and friends */
#  include <libintl.h>
#endif	/* HAVE_LIBINTL_H */

#include "pconn/pconn.h"
#include "cs_error.h"
#include "coldsync.h"
#include "pdb.h"
#include "conduit.h"
#include "parser.h"
#include "pref.h"

int sync_trace = 0;		/* Debugging level for sync-related stuff */
int misc_trace = 0;		/* Debugging level for miscellaneous stuff */

extern char *synclog;		/* Log that'll be uploaded to the Palm. See
				 * rant in "log.c".
				 */

/* XXX - This should be defined elsewhere (e.g., in a config file)
 * (Actually, it should be determined dynamically: try to figure out how
 * fast the serial port can go). The reason there are two macros here is
 * that under Solaris, B19200 != 19200 for whatever reason.
 */
/* These two macros specify the default sync rate. The reason it's so low
 * is that the serial port on a Sun Ultra 1 doesn't want to go any faster.
 * Of course, these are the same people who didn't define B38400 = 38400,
 * so what did you expect?
 */
#define SYNC_RATE		38400L
#define BSYNC_RATE		B38400

extern int load_palm_config(struct Palm *palm);
int CheckLocalFiles(struct Palm *palm);
int UpdateUserInfo(struct PConnection *pconn,
		   const struct Palm *palm, const int success);
int reserve_fd(int fd, int flags);

/* speeds
 * This table provides a list of speeds at which the serial port might be
 * able to communicate.
 * Its structure seems a bit silly, and it is, but only because there are
 * OSes out there that define the B<speed> constants to have values other
 * than their corresponding numeric values.
 */
static struct {
	udword bps;		/* Speed in bits per second, as used by the
				 * CMP layer.
				 */
	speed_t tcspeed;	/* Value to pass to cfset[io]speed() to set
				 * the speed to 'bps'.
				 */
} speeds[] = {
#ifdef B230400
	{ 230400,	B230400 },
#endif	/* B230400 */
#ifdef B115200
	{ 115200,	B115200 },
#endif	/* B115200 */
#ifdef B76800
	{ 76800,	B76800 },
#endif	/* B76800 */
#ifdef B57600
	{ 57600,	B57600 },
#endif	/* B57600 */
#ifdef B38400
	{ 38400,	B38400 },
#endif	/* B38400 */
#ifdef B28800
	{ 28800,	B28800 },
#endif	/* B28800 */
#ifdef B19200
	{ 19200,	B19200 },
#endif	/* B19200 */
#ifdef B14400
	{ 14400,	B14400 },
#endif	/* B14400 */
#ifdef B9600
	{  9600,	 B9600 },
#endif	/* B9600 */
#ifdef B7200
	{  7200,	 B7200 },
#endif	/* B7200 */
#ifdef B4800
	{  4800,	 B4800 },
#endif	/* B4800 */
#ifdef B2400
	{  2400,	 B2400 },
#endif	/* B2400 */
#ifdef B1200
	{  1200,	 B1200 },
#endif	/* B1200 */
	/* I doubt anyone wants to go any slower than 1200 bps */
};
#define num_speeds	sizeof(speeds) / sizeof(speeds[0])

int need_slow_sync;	/* XXX - This is bogus. Presumably, this should be
			 * another field in 'struct Palm' or something.
			 */

int cs_errno;			/* ColdSync error code. */
struct cmd_opts global_opts;	/* Command-line options */
struct sync_config *sync_config = NULL;
				/* Configuration for the current sync */
struct pref_item *pref_cache;	/* Preference cache */

int
main(int argc, char *argv[])
{
	int err;

	/* Initialize the global options to sane values */
	global_opts.mode		= mode_None;
	global_opts.conf_fname		= NULL;
	global_opts.conf_fname_given	= False;
	global_opts.devname		= NULL;
	global_opts.devtype		= -1;
	global_opts.do_backup		= False;
	global_opts.backupdir		= NULL;
	global_opts.do_restore		= False;
	global_opts.restoredir		= NULL;
	global_opts.force_install	= False;

	/* Initialize the debugging levels to 0 */
	slp_trace	= 0;
	cmp_trace	= 0;
	padp_trace	= 0;
	dlp_trace	= 0;
	dlpc_trace	= 0;
	sync_trace	= 0;
	pdb_trace	= 0;
	misc_trace	= 0;
	io_trace	= 0;

#if HAVE_GETTEXT
	/* Set things up so that i18n works. The constants PACKAGE and
	 * LOCALEDIR are strings set up in ../config.h by 'configure'.
	 */
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
#endif	/* HAVE_GETTEXT */

	/* Make sure that file descriptors 0-2 (stdin, stdout, stderr) are
	 * in use. This avoids some nasty problems that can occur when
	 * these file descriptors are initially closed (e.g., if ColdSync
	 * is run from a daemon). Specifically, open() opens the
	 * lowest-numbered available file descriptor. If stdin is closed,
	 * then a file that ColdSync opens might have file descriptor 0.
	 * Then, when ColdSync fork()s to run a conduit, it shuffles its
	 * file descriptors around to set stdin and stdout to specific
	 * values. If file descriptors 0 or 1 are important file
	 * descriptors, they will get clobbered.
	 *
	 * Stevens[1] addresses this problem to some extent, but his
	 * solution is limited to checking known file descriptors to make
	 * sure that they're not 0 (for stdin) or 1 (for stdout). This is
	 * fine for his example programs, but ColdSync opens too many files
	 * for this approach to work. Hence, we opt for a somewhat uglier
	 * solution and just make sure from the start that file descriptors
	 * 0-2 are in use.
	 *
	 * [1] Stevens, W. Richard, "Advanced Programming in the UNIX
	 * Environment", Addison-Wesley, 1993
	 */
	if (reserve_fd(0, O_RDONLY) < 0)
	{
		fprintf(stderr,
			_("Error: can't reserve file descriptor %d\n"), 0);
		exit(1);
	}
	if (reserve_fd(1, O_WRONLY) < 0)
	{
		fprintf(stderr,
			_("Error: can't reserve file descriptor %d\n"), 1);
		exit(1);
	}
	if (reserve_fd(2, O_RDONLY) < 0)
	{
		fprintf(stderr,
			_("Error: can't reserve file descriptor %d\n"), 2);
		exit(1);
	}

	/* Parse command-line arguments */
	err = parse_args(argc, argv);
	if (err < 0)
	{
		/* Error in command-line arguments */
		fprintf(stderr, _("Error parsing command-line arguments.\n"));
		exit(1);
	}
	if (err == 0)
		/* Not an error, but no need to go on (e.g., the user just
		 * wanted the usage message.
		 */
		exit(0);

	argc -= err;		/* Skip the parsed command-line options */
	argv += err;

	/* Load the configuration: read /etc/coldsync.conf, followed by
	 * ~/.coldsyncrc .
	 */
	if ((err = load_config()) < 0)
	{
		fprintf(stderr, _("Error loading configuration.\n"));
		exit(1);
	}

	MISC_TRACE(1)
		/* So I'll know what people are running when they send me
		 * stderr.
		 */
		print_version();

	MISC_TRACE(2)
	{
		fprintf(stderr, "Options:\n");
		fprintf(stderr, "\tMode: ");
		switch (global_opts.mode) {
		    case mode_None:
			fprintf(stderr, "* NONE *\n");
			break;
		    case mode_Standalone:
			fprintf(stderr, "Standalone\n");
			break;
		    case mode_Backup:
			fprintf(stderr, "Backup\n");
			break;
		    case mode_Restore:
			fprintf(stderr, "Restore\n");
			break;
		    case mode_Install:
			fprintf(stderr, "Install\n");
			break;
		    case mode_Daemon:
			fprintf(stderr, "Daemon\n");
			break;
		    case mode_Getty:
			fprintf(stderr, "Getty\n");
			break;
		    case mode_Init:
			fprintf(stderr, "Init\n");
			break;
		    default:
			fprintf(stderr, "* UNKNOWN *\n");
			break;
		}
		fprintf(stderr, "\tconf_fname: \"%s\"\n",
			global_opts.conf_fname == NULL ?
			"(null)" : global_opts.conf_fname);
		fprintf(stderr, "\tconf_fname_given: %s\n",
			global_opts.conf_fname_given ? "True" : "False");
		fprintf(stderr, "\tdevname: %s\n",
			global_opts.devname == NULL ?
			"(null)" : global_opts.devname);
		fprintf(stderr, "\tdevtype: %d\n",
			global_opts.devtype);
		fprintf(stderr, "\tdo_backup: %s\n",
			global_opts.do_backup ? "True" : "False");
		fprintf(stderr, "\tbackupdir: \"%s\"\n",
			global_opts.backupdir == NULL ?
			"(null)" : global_opts.backupdir);
		fprintf(stderr, "\tdo_restore: %s\n",
			global_opts.do_restore ? "True" : "False");
		fprintf(stderr, "\trestoredir: \"%s\"\n",
			global_opts.restoredir == NULL ?
				"(null)" : global_opts.restoredir);
		fprintf(stderr, "\tforce_slow: %s\n",
			global_opts.force_slow ? "True" : "False");
		fprintf(stderr, "\tforce_fast: %s\n",
			global_opts.force_fast ? "True" : "False");
		fprintf(stderr, "\tcheck_ROM: %s\n",
			global_opts.check_ROM ? "True" : "False");
		fprintf(stderr, "\tinstall_first: %s\n",
			global_opts.install_first ? "True" : "False");
		fprintf(stderr, "\tforce_install: %s\n",
			global_opts.force_install ? "True" : "False");
	}

	MISC_TRACE(3)
	{
		fprintf(stderr, "\nDebugging levels:\n");
		fprintf(stderr, "\tSLP:\t%d\n", slp_trace);
		fprintf(stderr, "\tCMP:\t%d\n", cmp_trace);
		fprintf(stderr, "\tPADP:\t%d\n", padp_trace);
		fprintf(stderr, "\tDLP:\t%d\n", dlp_trace);
		fprintf(stderr, "\tDLPC:\t%d\n", dlpc_trace);
		fprintf(stderr, "\tPDB:\t%d\n", pdb_trace);
		fprintf(stderr, "\tSYNC:\t%d\n", sync_trace);
		fprintf(stderr, "\tPARSE:\t%d\n", parse_trace);
		fprintf(stderr, "\tIO:\t%d\n", io_trace);
		fprintf(stderr, "\tMISC:\t%d\n", misc_trace);
	}

	/* Perform mode-specific actions */
	switch (global_opts.mode) {
	    case mode_None:		/* No mode specified */
	    case mode_Standalone:
		err = run_mode_Standalone(argc, argv);
		break;
	    case mode_Backup:
		err = run_mode_Backup(argc, argv);
		break;
	    case mode_Restore:
		err = run_mode_Restore(argc, argv);
		break;
#if 0
	    case mode_Install:
		err = run_mode_Install(argc, argv);
		break;
#endif	/* 0 */
	    default:
		/* This should never happen */
		fprintf(stderr,
			_("Error: unknown mode: %d\n"
			  "This is a bug. Please report it to the "
			  "maintainer.\n"),
			global_opts.mode);
		err = -1;
	}

	if (sync_config != NULL)
	{
		free_sync_config(sync_config);
		sync_config = NULL;
	}

	MISC_TRACE(6)
		fprintf(stderr, "Freeing pref_cache\n");
	FreePrefList(pref_cache);

	MISC_TRACE(1)
		fprintf(stderr, "ColdSync terminating normally\n");

	if (err < 0)
		exit(-err);
	exit(0);

	/* NOTREACHED */
}

int
run_mode_Standalone(int argc, char *argv[])
{
	int err;
	struct PConnection *pconn;	/* Connection to the Palm */
	struct pref_item *pref_cursor;
	const struct dlp_dbinfo *cur_db;
					/* Used when iterating over all
					 * databases */
	struct dlp_dbinfo dbinfo;	/* Used when installing files */
	struct Palm *palm;

	/* Get listen block */
	if (sync_config->listen == NULL)
	{
		fprintf(stderr, _("Error: no port specified.\n"));
		return -1;
	}

	/* XXX - If we're listening on a serial port, figure out fastest
	 * speed at which it will run.
	 */
	SYNC_TRACE(2)
		fprintf(stderr, "Opening device [%s]\n",
			sync_config->listen->device);

	/* Set up a PConnection to the Palm */
	/* XXX - set last parameter to zero to inhibit "Press HotSync
	 *  button prompt" when in daemon mode
	 */
	if ((pconn = new_PConnection(sync_config->listen->device,
				     sync_config->listen->listen_type, 1))
	    == NULL)
	{
		fprintf(stderr, _("Error: can't open connection.\n"));
		return -1;
	}
	pconn->speed = sync_config->listen->speed;

	/* Connect to the Palm */
	if ((err = Connect(pconn)) < 0)
	{
		fprintf(stderr, _("Can't connect to Palm\n"));
		PConnClose(pconn);
		return -1;
	}

	/* Allocate a new Palm description */
	if ((palm = new_Palm(pconn)) == NULL)
	{
		fprintf(stderr, _("Error: can't allocate struct Palm.\n"));
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
		return -1;
	}

	/* Get the Palm's serial number, if possible */
	if (palm_serial_len(palm) > 0)
	{
		char checksum;		/* Serial number checksum */

		/* Calculate the checksum for the serial number */
		checksum = snum_checksum(palm_serial(palm),
					 palm_serial_len(palm));
		SYNC_TRACE(2)
			fprintf(stderr, "Serial number is \"%s-%c\"\n",
				palm_serial(palm), checksum);
	}

	/* Figure out which Palm we're dealing with, and initialize
	 * per-palm config.
	 */
	/* XXX - This also creates ~/.palm/<stuff> . Perhaps that's not the
	 * right place for it?
	 */
	if ((err = load_palm_config(palm)) < 0)
	{
		fprintf(stderr, _("Can't get per-Palm config.\n"));
		free_Palm(palm);
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
		pconn = NULL;
		return -1;
	}

	/* XXX - In daemon mode, presumably load_palm_config() (or
	 * something) should tell us which user to run as. Therefore fork()
	 * an instance, have it setuid() to the appropriate user, and load
	 * that user's configuration.
	 */

	/* Initialize (per-user) conduits */
	MISC_TRACE(1)
		fprintf(stderr, "Initializing conduits\n");

	/* Initialize preference cache */
	MISC_TRACE(1)
		fprintf(stderr,"Initializing preference cache\n");
	if ((err = CacheFromConduits(sync_config->conduits, pconn)) < 0)
	{
		fprintf(stderr,
			_("CacheFromConduits() returned %d\n"), err);
		free_Palm(palm);
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
		pconn = NULL;
		return -1;
	}

	/* Find out whether we need to do a slow sync or not */
	/* XXX - Actually, it's not as simple as this (see comment below) */
	if (hostid == palm_lastsyncPC(palm))
		/* We synced with this same machine last time, so we can do
		 * a fast sync this time.
		 */
		need_slow_sync = 0;
	else
		/* The Palm synced with some other machine, the last time
		 * it synced. We need to do a slow sync.
		 */
		need_slow_sync = 1;

	/* XXX - The desktop needs to keep track of other hosts that it has
	 * synced with, preferably on a per-database basis.
	 * Scenario: I sync with the machine at home, whose hostID is 1.
	 * While I'm driving in to work, the machine at home talks to the
	 * machine at work, whose hostID is 2. I come in to work and sync
	 * with the machine there. The machine there should realize that
	 * even though the Palm thinks it has last synced with machine 1,
	 * everything is up to date on machine 2, so it's okay to do a fast
	 * sync.
	 * This is actually a good candidate for optimization, since it
	 * reduces the amount of time the user has to wait.
	 */

	palm_fetch_all_DBs(palm);	/* We're going to be looking at all
					 * of the databases on the Palm, so
					 * make sure we get them all.
					 */
	/* XXX - Error-checking */

	MISC_TRACE(1)
		fprintf(stderr, "Doing a sync.\n");


	/* Run any install conduits on the dbs in install directory Notice
	 * that install conduits are *not* run on files named for install
	 * on the command line.
	 */
	while (NextInstallFile(&dbinfo)>=0) {
		err = run_Install_conduits(&dbinfo);
		if (err < 0) {
			fprintf(stderr, 
				_("Error %d running install conduits.\n"),
				err);
			MISC_TRACE(6)
				fprintf(stderr,
					"Freeing pref_cache\n");
			FreePrefList(pref_cache);
			free_Palm(palm);
			Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
			pconn = NULL;
			return -1;
		}
	}

	/* Install new databases */
	/* XXX - It should be configurable whether new databases get
	 * installed at the beginning or the end.
	 */
	err = InstallNewFiles(pconn, palm, installdir, True);

	/* XXX - It should be possible to specify a list of directories to
	 * look in: that way, the user can put new databases in
	 * ~/.palm/install, whereas in a larger site, the sysadmin can
	 * install databases in /usr/local/stuff; they'll be uploaded from
	 * there, but not deleted.
	 */
	/* err = InstallNewFiles(pconn, &palm, "/tmp/palm-install",
			      False);*/
	if (err < 0)
	{
		fprintf(stderr, _("Error installing new files.\n"));

		MISC_TRACE(6)
			fprintf(stderr, "Freeing pref_cache\n");

		FreePrefList(pref_cache);
		free_Palm(palm);
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
		pconn = NULL;
		return -1;
	}

	/* For each database, walk config.fetch, looking for applicable
	 * conduits for each database.
	 */
	/* XXX - This should be redone: run_Fetch_conduits should be run
	 * _before_ opening the device. That way, they can be run even if
	 * we don't intend to sync with an actual Palm. Presumably, the
	 * Right Thing is to run the appropriate Fetch conduit for each
	 * file in ~/.palm/backup.
	 * OTOH, if you do it this way, then things won't work right the
	 * first time you sync: say you have a .coldsyncrc that has
	 * pre-fetch and post-dump conduits to sync with the 'kab'
	 * addressbook; you're syncing for the first time, so there's no
	 * ~/.palm/backup/AddressDB.pdb. In this case, the pre-fetch
	 * conduit doesn't get run, and the post-dump conduit overwrites
	 * the existing 'kab' database.
	 * Perhaps do things this way: run the pre-fetch conduits for the
	 * databases in ~/.palm/backup. Then, during the main sync, if a
	 * new database needs to be created on the workstation, run its
	 * pre-fetch conduit. (Or maybe this would be a good time to run
	 * the install conduit.)
	 */
	palm_resetdb(palm);
	while ((cur_db = palm_nextdb(palm)) != NULL)
	{
		err = run_Fetch_conduits(cur_db);
		if (err < 0)
		{
			fprintf(stderr, _("Error %d running pre-fetch "
					  "conduits.\n"),
				err);

			MISC_TRACE(6)
				fprintf(stderr,
					"Freeing pref_cache\n");
			FreePrefList(pref_cache);
			free_Palm(palm);
			Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
			pconn = NULL;
			return -1;
		}
	}

	/* Synchronize the databases */
	palm_resetdb(palm);
	while ((cur_db = palm_nextdb(palm)) != NULL)
	{
		/* Run the Sync conduits for this database. This includes
		 * built-in conduits.
		 */
		err = run_Sync_conduits(cur_db, pconn);
		if (err < 0)
		{
			switch (cs_errno)
			{
			    case CSE_CANCEL:
				fprintf(stderr,
					_("Sync cancelled.\n"));
				add_to_log(_("*Cancelled*\n"));
				DlpAddSyncLogEntry(pconn, synclog);
					/* Doesn't really matter if it
					 * fails, since we're terminating
					 * anyway.
					 */
				break;
				/* XXX - Other reasons for premature
				 * termination.
				 */
			    default:
				fprintf(stderr,
					_("Conduit failed for unknown "
					  "reason\n"));
				/* Continue, and hope for the best */
				/*  break; */
				continue;
			}

			free_Palm(palm);
			Disconnect(pconn, DLPCMD_SYNCEND_OTHER);
			pconn = NULL;

			MISC_TRACE(6)
				fprintf(stderr, "Freeing pref_cache\n");
			FreePrefList(pref_cache);
			return -1;
		}
	}

	/* XXX - If it's configured to install new databases last, install
	 * new databases now.
	 */

	/* Get list of local files: if there are any that aren't on the
	 * Palm, it probably means that they existed once, but were deleted
	 * on the Palm. Assuming that the user knew what he was doing,
	 * these databases should be deleted. However, just in case, they
	 * should be saved to an "attic" directory.
	 */
	err = CheckLocalFiles(palm);

	/* XXX - Write updated NetSync info */
	/* Write updated user info */
	if ((err = UpdateUserInfo(pconn, palm, 1)) < 0)
	{
		fprintf(stderr, _("Error writing user info\n"));
		free_Palm(palm);
		Disconnect(pconn, DLPCMD_SYNCEND_OTHER);
		pconn = NULL;

		MISC_TRACE(6)
			fprintf(stderr, "Freeing pref_cache\n");
		FreePrefList(pref_cache);
		return -1;
	}

	/* Upload sync log */
	if (synclog != NULL)
	{
		SYNC_TRACE(2)
			fprintf(stderr, "Writing log to Palm\n");

		if ((err = DlpAddSyncLogEntry(pconn, synclog)) < 0)
		{
			fprintf(stderr, _("Error writing sync log.\n"));
			free_Palm(palm);
			Disconnect(pconn, DLPCMD_SYNCEND_OTHER);
			pconn = NULL;

			MISC_TRACE(6)
				fprintf(stderr, "Freeing pref_cache\n");
			FreePrefList(pref_cache);
			return -1;
		}
	}

	/* There might still be pref items that are not yet cached.
	 * The dump conduits are going to try to download them if not cached,
	 * but with a closed connection, they are gonna fail. Although ugly,
	 * we will manually fill the cache here.
	 */
	for (pref_cursor = pref_cache;
	     pref_cursor != NULL;
	     pref_cursor = pref_cursor->next)
	{
		MISC_TRACE(5)
			fprintf(stderr, "Fetching preference 0x%08lx/%d\n",
				pref_cursor->description.creator,
				pref_cursor->description.id);

		if (pref_cursor->contents_info == NULL &&
		    pconn == pref_cursor->pconn)
			FetchPrefItem(pconn, pref_cursor);
	}

	/* Finally, close the connection */
	SYNC_TRACE(3)
		fprintf(stderr, "Closing connection to Palm\n");

	if ((err = Disconnect(pconn, DLPCMD_SYNCEND_NORMAL)) < 0)
	{
		fprintf(stderr, _("Error disconnecting\n"));
		return -1;
	}

	pconn = NULL;

	/* Run Dump conduits */
	palm_resetdb(palm);
	while ((cur_db = palm_nextdb(palm)) != NULL)
	{
		err = run_Dump_conduits(cur_db);
		if (err < 0)
		{
			fprintf(stderr,
				_("Error %d running post-dump conduits.\n"),
				err);
			break;
		}
	}

	free_Palm(palm);

	return 0;
}

int
run_mode_Backup(int argc, char *argv[])
{
	int err;
	const char *backupdir = NULL;	/* Where to put backup */
	struct PConnection *pconn;	/* Connection to the Palm */
	struct Palm *palm;

	/* Parse arguments:
	 *	dir		- Dump everything to <dir>
	 *	dir file...	- Dump <file...> to <dir>
	 */
	if (global_opts.do_backup)
	{
		/* Compatibility mode: the user specified "-b <dir>" rather
		 * than the newer "-mb ... <stuff>". Back everything up to
		 * the specified backup directory.
		 */
		backupdir = global_opts.backupdir; 
	} else {
		if (argc == 0)
		{
			fprintf(stderr,
				_("Error: no backup directory specified.\n"));
			return -1;
		}

		backupdir = argv[0];
		argc--;
		argv++;
	}

	SYNC_TRACE(2)
		fprintf(stderr, "Backup directory: \"%s\"\n", backupdir);
	if (!is_directory(backupdir))
	{
		/* It would be easy enough to create the directory if it
		 * doesn't exist. However, this would be fragile: if the
		 * user meant to back up databases "a", "b" and "c", and
		 * erroneously said
		 *	coldsync -mb a b c
		 * then ColdSync would silently create the directory "a"
		 * and surprise the user.
		 */
		if (exists(NULL))
		{
			fprintf(stderr,
				_("Error: \"%s\" exists, but is not a "
				  "directory.\n"),
				backupdir);
		} else {
			fprintf(stderr,
				_("Error: no such directory: \"%s\"\n"),
				backupdir);
		}

		return -1;
	}

	/* Get listen block */
	if (sync_config->listen == NULL)
	{
		fprintf(stderr, _("Error: no port specified.\n"));
		return -1;
	}

	/* XXX - If we're listening on a serial port, figure out fastest
	 * speed at which it will run.
	 */
	SYNC_TRACE(2)
		fprintf(stderr, "Opening device [%s]\n",
			sync_config->listen->device);

	/* Set up a PConnection to the Palm */
	if ((pconn = new_PConnection(sync_config->listen->device,
				     sync_config->listen->listen_type, 1))
	    == NULL)
	{
		fprintf(stderr, _("Error: can't open connection.\n"));
		return -1;
	}
	pconn->speed = sync_config->listen->speed;

	/* Connect to the Palm */
	if ((err = Connect(pconn)) < 0)
	{
		fprintf(stderr, _("Can't connect to Palm\n"));
		PConnClose(pconn);
		return -1;
	}

	/* Allocate a new Palm description */
	if ((palm = new_Palm(pconn)) == NULL)
	{
		fprintf(stderr, _("Error: can't allocate struct Palm.\n"));
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
		return -1;
	}

	/* Do the backup */
	if (argc == 0)
	{
		/* No databases named on the command line. Back everything
		 * up.
		 */
		SYNC_TRACE(2)
			fprintf(stderr, "Backing everything up.\n");

		err = full_backup(pconn, palm, backupdir);
	} else {
		/* Individual databases were listed on the command line.
		 * Back them up.
		 */
		int i;

		for (i = 0; i < argc; i++)
		{
			const struct dlp_dbinfo *db;

			SYNC_TRACE(2)
				fprintf(stderr,
					"Backing up database \"%s\"\n",
					argv[i]);

			db = palm_find_dbentry(palm, argv[i]);
					/* Find the dlp_dbentry for this
					 * database.
					 */
			if (db == NULL)
			{
				fprintf(stderr,
					_("Warning: no such database: "
					  "\"%s\"\n"),
					argv[i]);
				add_to_log(_("Backup "));
				add_to_log(argv[i]);
				add_to_log(" - ");
				add_to_log(_("No such database\n"));
				continue;
			}

			/* Back up the database */
			err = backup(pconn, db, backupdir);
			/* XXX - Error-checking */
		}
	}

	/* Upload sync log */
	if (synclog != NULL)
	{
		SYNC_TRACE(2)
			fprintf(stderr, "Writing log to Palm\n");

		if ((err = DlpAddSyncLogEntry(pconn, synclog)) < 0)
		{
			fprintf(stderr, _("Error writing sync log.\n"));
			free_Palm(palm);
			Disconnect(pconn, DLPCMD_SYNCEND_OTHER);
			pconn = NULL;
			return -1;
		}
	}

	/* Finally, close the connection */
	SYNC_TRACE(3)
		fprintf(stderr, "Closing connection to Palm\n");

	free_Palm(palm);
	palm = NULL;

	if ((err = Disconnect(pconn, DLPCMD_SYNCEND_NORMAL)) < 0)
	{
		fprintf(stderr, _("Error disconnecting\n"));
		return -1;
	}
	pconn = NULL;

	return 0;
}

int
run_mode_Restore(int argc, char *argv[])
{
	int err;
	int i;
	struct PConnection *pconn;	/* Connection to the Palm */
	struct Palm *palm;

	/* Get listen block */
	if (sync_config->listen == NULL)
	{
		fprintf(stderr, _("Error: no port specified.\n"));
		return -1;
	}

	/* XXX - If we're listening on a serial port, figure out fastest
	 * speed at which it will run.
	 */
	SYNC_TRACE(2)
		fprintf(stderr, "Opening device [%s]\n",
			sync_config->listen->device);

	/* Set up a PConnection to the Palm */
	if ((pconn = new_PConnection(sync_config->listen->device,
				     sync_config->listen->listen_type, 1))
	    == NULL)
	{
		fprintf(stderr, _("Error: can't open connection.\n"));
		return -1;
	}
	pconn->speed = sync_config->listen->speed;

	/* Connect to the Palm */
	if ((err = Connect(pconn)) < 0)
	{
		fprintf(stderr, _("Can't connect to Palm\n"));
		PConnClose(pconn);
		return -1;
	}

	/* Allocate a new Palm description */
	if ((palm = new_Palm(pconn)) == NULL)
	{
		fprintf(stderr, _("Error: can't allocate struct Palm.\n"));
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
		return -1;
	}

	/* Parse arguments: for each argument, if it's a file, upload that
	 * file. If it's a directory, upload all databases in that
	 * directory.
	 */
	if (global_opts.do_restore)
	{
		/* Compatibility mode: the user has specified "-mr <dir>".
		 * Restore everything in <dir>.
		 */
		err = restore_dir(pconn, palm, global_opts.backupdir);
		/* XXX - Error-checking */
	} else {
		for (i = 0; i < argc; i++)
		{
			if (is_directory(argv[i]))
			{
				/* Restore all databases in argv[i] */

				err = restore_dir(pconn, palm, argv[i]);
				/* XXX - Error-checking */
			} else {
				/* Restore the file argv[i] */

				err = restore_file(pconn, palm, argv[i]);
				/* XXX - Error-checking */
			}
		}
	}

	/* Upload sync log */
	if (synclog != NULL)
	{
		SYNC_TRACE(2)
			fprintf(stderr, "Writing log to Palm\n");

		if ((err = DlpAddSyncLogEntry(pconn, synclog)) < 0)
		{
			fprintf(stderr, _("Error writing sync log.\n"));
			free_Palm(palm);
			Disconnect(pconn, DLPCMD_SYNCEND_OTHER);
			pconn = NULL;
			return -1;
		}
	}

	/* Finally, close the connection */
	SYNC_TRACE(3)
		fprintf(stderr, "Closing connection to Palm\n");

	if ((err = Disconnect(pconn, DLPCMD_SYNCEND_NORMAL)) < 0)
	{
		fprintf(stderr, _("Error disconnecting\n"));
		return -1;
	}
	pconn = NULL;

	free_Palm(palm);

	return 0;
}

/* XXX - run_mode_Daemon() */
/* XXX
int
run_mode_Daemon()
{
	XXX - Read command-line arguments
	XXX - Read /etc/coldsync.conf
	XXX - Find listen block(s)
	XXX - Listen; wait for connection
	XXX - Get connection
	XXX - fork()
	XXX - Identify Palm (serial number?)
	XXX - Identify user (Palm userid?)
	XXX - setuid()
	XXX - Read ~user/.coldsyncrc
	XXX - Find appropriate PDA block
	XXX - Sync normally
}
*/
/* XXX - run_mode_Getty() */
/* XXX
int
run_mode_Getty()
{
	XXX - Read command-line arguments
	XXX - Read /etc/coldsync.conf
	XXX - Find listen block (mainly for speed)
	XXX - Listen; wait for connection
	XXX - Get connection
	XXX - Identify Palm (serial number?)
	XXX - Identify user (Palm userid?)
	XXX - setuid()
	XXX - Read ~user/.coldsyncrc
	XXX - Find appropriate PDA block
	XXX - Sync normally
}
*/
/* XXX - run_mode_Init() */

/* Connect
 * Wait for a Palm to show up on the other end.
 */
/* XXX - This ought to be able to listen to a whole list of files and
 * establish a connection with the first one that starts talking. This
 * might also be the place to fork().
 */
int
Connect(struct PConnection *pconn)
{
	int err;
	struct slp_addr pcaddr;
	struct cmp_packet cmpp;
	udword bps = SYNC_RATE;		/* Connection speed, in bps */
	speed_t tcspeed = BSYNC_RATE;	/* B* value corresponding to 'bps'
					 * (a necessary distinction because
					 * some OSes (hint to Sun!) have
					 * B19200 != 19200.
					 */

	pcaddr.protocol = SLP_PKTTYPE_PAD;	/* XXX - This ought to be
						 * part of the initial
						 * socket setup.
						 */
	pcaddr.port = SLP_PORT_DLP;
	PConn_bind(pconn, &pcaddr);
	/* XXX - PConn_accept(fd) */

	SYNC_TRACE(5)
		fprintf(stderr, "===== Waiting for wakeup packet\n");
	do {
		err = cmp_read(pconn, &cmpp);
		if (err < 0)
		{
			if (palm_errno == PALMERR_TIMEOUT)
				continue;
			fprintf(stderr, _("Error during cmp_read: (%d) %s\n"),
				palm_errno,
				_(palm_errlist[palm_errno]));
			exit(1); /* XXX */
		}
	} while (cmpp.type != CMP_TYPE_WAKEUP);

	SYNC_TRACE(5)
		fprintf(stderr, "===== Got a wakeup packet\n");

	/* Find the speed at which to connect */
	if (pconn->speed != 0)
	{
		/* The .coldsyncrc specifies a speed */
		int i;

		SYNC_TRACE(3)
			fprintf(stderr, "pconn->speed == %ld\n",
				pconn->speed);

		/* Go through the speed table. Make sure the requested
		 * speed appears in the table: this is to make sure that
		 * there is a valid B* speed to pass to cfsetspeed().
		 */
		for (i = 0; i < num_speeds; i++)
		{
			SYNC_TRACE(7)
				fprintf(stderr, "Comparing %ld ==? %ld\n",
					speeds[i].bps, pconn->speed);

			if (speeds[i].bps == pconn->speed)
			{
				/* Found it */
				SYNC_TRACE(7)
					fprintf(stderr, "Found it\n");
				bps = speeds[i].bps;
				tcspeed = speeds[i].tcspeed;
				break;
			}
		}

		if (i >= num_speeds)
		{
			/* The requested speed wasn't found */
			fprintf(stderr, _("Warning: can't set the speed you "
					  "requested (%ld bps).\nUsing "
					  "default (%ld bps)\n"),
				pconn->speed,
				SYNC_RATE);
			pconn->speed = 0L;
		}
	}

	if (pconn->speed == 0)
	{
		/* Either the .coldsyncrc didn't specify a speed, or else
		 * the one that was specified was bogus.
		 */
		SYNC_TRACE(2)
			fprintf(stderr, "Using default speed (%ld bps)\n",
				SYNC_RATE);
		bps = SYNC_RATE;
		tcspeed = BSYNC_RATE;
	}
	SYNC_TRACE(2)
		fprintf(stderr, "-> Setting speed to %ld (%ld)\n",
			(long) bps, (long) tcspeed);

	/* Compose a reply */
	cmpp.type = CMP_TYPE_INIT;
	cmpp.ver_major = 1;	/* XXX - Should be constants in header file */
	cmpp.ver_minor = 1;
	cmpp.rate = bps;
	cmpp.flags = CMP_IFLAG_CHANGERATE;

	SYNC_TRACE(5)
		fprintf(stderr, "===== Sending INIT packet\n");
	cmp_write(pconn, &cmpp);	/* XXX - Error-checking */

	SYNC_TRACE(5)
		fprintf(stderr, "===== Finished sending INIT packet\n");

	/* Change the speed */
	/* XXX - This probably goes in Pconn_accept() or something */

	if ((err = (*pconn->io_setspeed)(pconn, tcspeed)) < 0)
	{
		fprintf(stderr, _("Error trying to set speed"));
		return -1;
	}
	return 0;
}

int
Disconnect(struct PConnection *pconn, const ubyte status)
{
	int err;

	/* Terminate the sync */
	err = DlpEndOfSync(pconn, status);
	if (err < 0)
	{
		fprintf(stderr, _("Error during DlpEndOfSync: (%d) %s\n"),
			palm_errno,
			_(palm_errlist[palm_errno]));
		PConnClose(pconn);
		return err;
	}
	SYNC_TRACE(5)
		fprintf(stderr, "===== Finished syncing\n");

	PConnClose(pconn);		/* Close the connection */

	return 0;
}

/* CheckLocalFiles
 * Clean up the backup directory: if there are any database files in it
 * that aren't installed on the Palm, move them to the attic directory, out
 * of the way, where they won't cause confusion.
 * Presumably, these files are databases that were deleted on the Palm. We
 * don't want to just delete them, because they may have been deleted
 * accidentally (for instance, the user may have synced with a fresh new
 * Palm), or they may still be valuable. Better to just move them out of
 * the way and let the user delete them manually.
 */
int
CheckLocalFiles(struct Palm *palm)
{
	int err;
	DIR *dir;
	struct dirent *file;

	MISC_TRACE(2)
		fprintf(stderr,
			"Checking for extraneous local files in \"%s\".\n",
			backupdir);

	if ((dir = opendir(backupdir)) == NULL)
	{
		fprintf(stderr, _("%s: can't open \"%s/\"\n"),
			"CheckLocalFiles",
			backupdir);
		perror("opendir");
		return -1;
	}

	/* Check each file in the directory in turn */
	while ((file = readdir(dir)) != NULL)
	{
		const struct dlp_dbinfo *db;
		const char *dbname;	/* Database name, built from file
					 * name */
		static char fromname[MAXPATHLEN+1];
					/* Full pathname of the database */
		static char toname[MAXPATHLEN+1];
					/* Pathname to which we'll move the
					 * database.
					 */
		int n;


		MISC_TRACE(4)
			fprintf(stderr,
				"CheckLocalFiles: Checking file \"%s\"\n",
				file->d_name);

		/* Construct the database name from the file name, and make
		 * sure it's sane.
		 */
		dbname = fname2dbname(file->d_name);
		if (dbname == NULL)
		{
			/* Not a valid database name */
			MISC_TRACE(4)
				fprintf(stderr, "CheckLocalFiles: \"%s\" not "
					"a valid database name\n",
					file->d_name);
			continue;
		}

		/* See if this database exists on the Palm */
		MISC_TRACE(4)
			fprintf(stderr,
				"Checking for \"%s\" on the Palm\n",
				dbname);
		if ((db = palm_find_dbentry(palm, dbname)) != NULL)
				/* It exists. Ignore it */
			continue;

		/* XXX - Walk through config.uninstall to find any *
		 * applicable conduits. Tell them that this database has
		 * been deleted. When they're done, if the database file
		 * still exists, move it to the attic.
		 */

		/* XXX - Might be a good idea to move this into its own
		 * function in "util.c": cautious_mv(from,to);
		 */
		/* This database doesn't exist on the Palm. Presumably, it
		 * was deleted on the Palm; hence, we should move it
		 * locally to the Attic directory, where it won't cause
		 * confusion. If the user didn't mean to delete the file,
		 * he can always move it from there to the install
		 * directory and reinstall.
		 */
		MISC_TRACE(3)
			fprintf(stderr,
				"Moving \"%s\" to the attic.\n",
				file->d_name);

		/* Come up with a unique filename to which to move the
		 * database.
		 * We don't use mkbakfname() because we already know that
		 * this database doesn't exist on the Palm, so we don't
		 * really care if bogus characters are escaped or not.
		 */

		/* Get the database's current full pathname */
		strncpy(fromname, backupdir, MAXPATHLEN);
		strncat(fromname, "/", MAXPATHLEN - strlen(fromname));
		strncat(fromname, file->d_name,
			MAXPATHLEN - strlen(fromname));

		/* First try at a destination pathname:
		 * <atticdir>/<filename>.
		 */
		strncpy(toname, atticdir, MAXPATHLEN);
		strncat(toname, "/", MAXPATHLEN - strlen(toname));
		strncat(toname, file->d_name,
			MAXPATHLEN - strlen(toname));

		/* Check to see if the destination filename exists. We use
		 * lstat() because, although we don't care if it's a
		 * symlink, we don't want to clobber that symlink if it
		 * exists.
		 */
		if (!lexists(toname))
		{
			/* The file doesn't exist. Move the database to the
			 * attic.
			 */
			MISC_TRACE(5)
				fprintf(stderr,
					"Renaming \"%s\" to \"%s\"\n",
					fromname, toname);
			err = rename(fromname, toname);
			if (err < 0)
			{
				fprintf(stderr, _("%s: Can't rename \"%s\" "
						  "to \"%s\"\n"),
					"CheckLocalFiles",
					fromname, toname);
				perror("rename");
				closedir(dir);
				return -1;
			}

			continue;	/* Go on to the next database. */
		}

		/* The proposed destination filename already exists. Try
		 * appending "~N" to the destination filename, where N is
		 * some number. Set 100 as the upper limit for N.
		 */
		for (n = 0; n < 100; n++)
		{
			static char nbuf[5];

			/* Write out the new extension */
			sprintf(nbuf, "~%d", n);

			/* Construct the proposed destination name:
			 * <atticdir>/<filename>~<n>
			 */
			strncpy(toname, atticdir, MAXPATHLEN);
			strncat(toname, "/",
				MAXPATHLEN - strlen(toname));
			strncat(toname, file->d_name,
				MAXPATHLEN - strlen(toname));
			strncat(toname, nbuf,
				MAXPATHLEN - strlen(toname));

			/* Check to see whether this file exists */
			if (lexists(toname))
				continue;

			/* The file doesn't exist. Move the database to the
			 * attic.
			 */
			MISC_TRACE(5)
				fprintf(stderr,
					"Renaming \"%s\" to \"%s\"\n",
					fromname, toname);
			err = rename(fromname, toname);
			if (err < 0)
			{
				fprintf(stderr, _("%s: Can't rename \"%s\" "
						  "to \"%s\"\n"),
					"CheckLocalFiles",
					fromname, toname);
				perror("rename");
				closedir(dir);
				return -1;
			}

			break;	/* Go on to the next database */
		}

		if (n >= 100)
		{
			/* Gah. All of the files "foo", "foo~0", "foo~1",
			 * through "foo~99" are all taken. This is bad, but
			 * there's nothing we can do about that right now.
			 */
			fprintf(stderr,
				_("Too many files named \"%s\" in the attic.\n"),
				file->d_name);
		}
	}
	closedir(dir);

	return 0;
}

/* UpdateUserInfo
 * Update the Palm's user info. 'success' indicates whether the sync was
 * successful.
 */
int
UpdateUserInfo(struct PConnection *pconn,
	       const struct Palm *palm,
	       const int success)
{
	int err;
	struct dlp_setuserinfo uinfo;
			/* Fill this in with new values */

	uinfo.userid = 0;		/* Mainly to make Purify happy */
	uinfo.viewerid = 0;		/* XXX - What is this, anyway? */
	uinfo.modflags = 0;		/* Initialize modification flags */
	uinfo.usernamelen = 0;

	MISC_TRACE(1)
		fprintf(stderr, "Updating user info.\n");

	/* If the Palm doesn't have a user ID, or if it's the wrong one,
	 * update it.
	 */
	/* XXX - Is it a mistake to update the userid/username if it
	 * doesn't match?
	 */
	/* XXX - For now, don't update the userid if there's an existing
	 * one on the Palm, but doesn't match. This is primarily because
	 * I've taken out the global 'userinfo' variable, but also partly
	 * because it may be a mistake to update it: changing the userid
	 * can break things if the user is also using HotSync under
	 * Windows.
	 */
	/* XXX - Should update the userid, but from the PDA block in
	 * .coldsyncrc, not from /etc/passwd.
	 */
#if 0
	if ((palm->userinfo.userid == 0) ||
	    (palm->userinfo.userid != userinfo.uid))
	{
		MISC_TRACE(3)
			fprintf(stderr, "Setting UID to %d (0x%04x)\n",
				(int) userinfo.uid,
				(unsigned int) userinfo.uid);
		uinfo.userid = (udword) userinfo.uid;
		uinfo.modflags |= DLPCMD_MODUIFLAG_USERID;
					/* Set modification flag */
	}
#endif	/* 0 */

	/* Fill in this machine's host ID as the last sync PC */
	MISC_TRACE(3)
		fprintf(stderr, "Setting lastsyncPC to 0x%08lx\n", hostid);
	uinfo.lastsyncPC = hostid;
	uinfo.modflags |= DLPCMD_MODUIFLAG_SYNCPC;

	/* If successful, update the "last successful sync" date */
	if (success)
	{
		time_t now;		/* Current time */

		MISC_TRACE(3)
			fprintf(stderr, "Setting last sync time to now\n");
		time(&now);		/* Get current time */
		time_time_t2dlp(now, &uinfo.lastsync);
					/* Convert to DLP time */
		uinfo.modflags |= DLPCMD_MODUIFLAG_SYNCDATE;
	}

	/* Fill in the user name if there isn't one, or if it has changed
	 */
	/* XXX - Ought to update the user name, but from the PDA block in
	 * .coldsyncrc, not from /etc/passwd.
	 */
#if 0
	if ((palm->userinfo.usernamelen == 0) ||
	    (strcmp(palm->userinfo.username, userinfo.fullname) != 0))
	{
		MISC_TRACE(3)
			fprintf(stderr, "Setting user name to \"%s\"\n",
				userinfo.fullname);
		uinfo.username = userinfo.fullname;
		uinfo.usernamelen = strlen(uinfo.username)+1;
		uinfo.modflags |= DLPCMD_MODUIFLAG_USERNAME;
		MISC_TRACE(3)
			fprintf(stderr, "User name length == %d\n",
				uinfo.usernamelen);
	}
#endif	/* 0 */

	/* Send the updated user info to the Palm */
	err = DlpWriteUserInfo(pconn,
			       &uinfo);
	if (err != DLPSTAT_NOERR)
	{
		fprintf(stderr, _("DlpWriteUserInfo failed: %d\n"), err);
		return -1;
	}

	return 0;		/* Success */
}

/* XXX - Not used anywhere yet */
/* find_max_speed
 * Find the maximum speed at which the serial port (pconn->fd) is able to
 * talk. Returns an index into the 'speeds' array, or -1 in case of error.
 * XXX - Okay, now where does this get called? The logical place is right
 * after the open() in new_PConnection(). But the result from this function
 * is used in this file, when setting the speed for the main part of the
 * sync.
 */
int
find_max_speed(struct PConnection *pconn)
{
	int i;

	/* Step through the array of speeds, one at a time. Stop as soon as
	 * we find a speed that works, since the 'speeds' array is sorted
	 * in order of decreasing desirability (i.e., decreasing speed).
	 */
	for (i = 0; i < num_speeds; i++)
	{
		if ((*pconn->io_setspeed)(pconn, speeds[i].tcspeed) == 0)
			return i;
	}

	return -1;
}

/* dbinfo_fill
 * Fill in a dbinfo record from a pdb record header.
 */
int
dbinfo_fill(struct dlp_dbinfo *dbinfo,
	    struct pdb *pdb)
{
	dbinfo->size = 0;		/* XXX - Bogus, but I don't think
					 * this is used anywhere.
					 */
	dbinfo->misc_flags = 0x40;	/* Kind of bogus, but it probably
					 * doesn't matter. FWIW, 0x40 means
					 * "RAM-based".
					 */
	dbinfo->db_flags = pdb->attributes;
	dbinfo->type = pdb->type;
	dbinfo->creator = pdb->creator;
	dbinfo->version = pdb->version;
	dbinfo->modnum = pdb->modnum;

	time_palmtime2dlp(pdb->ctime, &(dbinfo->ctime));
	time_palmtime2dlp(pdb->mtime, &(dbinfo->mtime));
	time_palmtime2dlp(pdb->baktime, &(dbinfo->baktime));
	dbinfo->index =	0;		/* Bogus, but I don't think this is
					 * ever used.
					 */
	strncpy(dbinfo->name, pdb->name, DLPCMD_DBNAME_LEN);

	return 0;		/* Success */
}

/* snum_checksum
 * Calculate and return the checksum character for a checksum 'snum' of
 * length 'len'.
 */
char
snum_checksum(const char *snum, int len)
{
	int i;
	unsigned char checksum;

	checksum = 0;
	for (i = 0; i < len; i++)
	{
		checksum += toupper((int) snum[i]);
		checksum = (checksum << 1) |
			(checksum & 0x80 ? 1 : 0);
	}
	checksum = (checksum >> 4) + (checksum & 0x0f) + 2;
		/* The "+2" is there so that the checksum won't be '0' or
		 * '1', which are too easily confused with the characters
		 * 'O' and 'I'.
		 */

	/* Convert to a character and return it */
	return (char) (checksum < 10 ?
		       checksum + '0' :
		       checksum - 10 + 'A');
}

/* open_tempfile
 * Given a filename template (similar, not coincidentally, to mktemp()),
 * generates a unique filename and opens it for writing.
 * Returns a file descriptor for this file. It is the caller's
 * responsibility to close it.

 * NB: 'name_template' is a 'char *', and not a 'const char *', since
 * mktemp() modifies it.
 */
int
open_tempfile(char *name_template)
{
	int retval;

/* Apparently, Windows has mkstemp(), but it doesn't define O_BINARY on the
 * temporary file. So we can't use mkstemp().
 */
#if HAVE_MKSTEMP && !defined(_WIN32)

	retval =  mkstemp(name_template);
	if (retval < 0)
	{
		fprintf(stderr, _("%s: Can't create staging file \"%s\"\n"),
			"open_tempfile", name_template);
		perror("mkstemp");
		return -1;
	}

#else	/* HAVE_MKSTEMP */

	if (mktemp(name_template) == 0)
	{
		fprintf(stderr, _("%s: Can't create staging file name\n"),
			"open_tempfile");
		return -1;
	}

	/* Open the output file */
	if ((retval = open(name_template,
			   O_WRONLY | O_CREAT | O_EXCL | O_BINARY,
			   0600)) < 0)
	{
		fprintf(stderr, _("%s: Can't create staging file \"%s\"\n"),
			"open_tempfile", name_template);
		return -1;
	}

#endif	/* HAVE_MKSTEMP */

	return retval;
}

/* reserve_fd
 * Make sure that file descriptor number 'fd' is in use. This allows us to
 * guarantee that, even if stdin, stdout or stderr is closed (e.g., if
 * ColdSync was run by a daemon), file descriptors 0-2 are nonetheless
 * valid. This avoids headaches down the road.
 * Returns 0 if successful, or a negative value otherwise.
 */
int
reserve_fd(int fd,		/* File descriptor to check */
	   int flags)		/* Flags to pass to open(), if necessary */
{
	int err;
	int new_fd;

	/* BTW, these MISC_TRACE() statements are pretty useless, since the
	 * command-line arguments haven't been read yet.
	 */
	MISC_TRACE(7)
		fprintf(stderr, "reserve_fd: checking fd %d\n", fd);

	/* See if 'fd' is open. We do this by calling a semi-random
	 * function (in this case, fcntl(F_GETFD)), and seeing if it fails
	 * with errno == EBADF.
	 */
	if (((err = fcntl(fd, F_GETFD)) >= 0) ||
	    (errno != EBADF))
	{
		/* 'fd' is an active file descriptor. Don't do anything */
		MISC_TRACE(6)
			fprintf(stderr, "reserve_fd: doing nothing.\n");
		return 0;
	}

	MISC_TRACE(6)
		/* No, I haven't missed the potential problems inherent in
		 * writing to stderr in a function that checks to see
		 * whether stderr is closed. I just don't know what to do
		 * about it.
		 */
		fprintf(stderr, "reserve_fd: fd %d is closed\n",
			fd);

	/* The file descriptor is closed. Open it to /dev/null */
	if ((new_fd = open("/dev/null", flags)) < 0)
	{
		/* This can't happen, can it? */
		MISC_TRACE(6)
			perror("reserve_fd: open");
		return -1;
	}

	/* Make sure we've opened the right file descriptor: copy the new
	 * file descriptor to 'fd' if it isn't already.
	 */
	if (new_fd != fd)
	{
		MISC_TRACE(6)
			fprintf(stderr, "reserve_fd: moving fd %d to %d\n",
				new_fd, fd);

		if (dup2(new_fd, fd) != fd)
		{
			perror("reserve_fd: dup2");
			return -1;
		}
		close(new_fd);
	}

	return 0;		/* Success */
}

/* This is for Emacs's benefit:
 * Local Variables:	***
 * fill-column:	75	***
 * End:			***
 */