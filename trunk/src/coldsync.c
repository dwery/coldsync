/* coldsync.c
 *
 *	Copyright (C) 1999, 2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: coldsync.c,v 1.22 2000-01-25 11:26:04 arensb Exp $
 */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>		/* For malloc(), atoi() */
#include <sys/param.h>		/* For MAXPATHLEN */
#include <sys/types.h>		/* For stat() */
#include <sys/stat.h>		/* For stat() */
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
#if HAVE_LIBINTL
#  include <locale.h>		/* For setlocale() and friends */
#  include <libintl.h>
#endif	/* HAVE_LIBINTL */

#include "pconn/pconn.h"
#include "coldsync.h"
#include "pdb.h"
#include "conduit.h"
#include "parser.h"

#if !HAVE_STRCASECMP
#  define	strcasecmp(s1,s2)	strcmp((s1),(s2))
#endif	/* HAVE_STRCASECMP */
#if !HAVE_STRNCASECMP
#  define	strncasecmp(s1,s2,len)	strncmp((s1),(s2),(len))
#endif	/* HAVE_STRNCASECMP */

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
/*  #define SYNC_RATE		57600 */
/*  #define BSYNC_RATE		B57600 */
#define SYNC_RATE		38400
#define BSYNC_RATE		B38400

extern int load_palm_config(struct Palm *palm);
int CheckLocalFiles(struct Palm *palm);
int GetPalmInfo(struct PConnection *pconn, struct Palm *palm);
int UpdateUserInfo(struct PConnection *pconn,
		   const struct Palm *palm, const int success);

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
};
#define num_speeds	sizeof(speeds) / sizeof(speeds[0])

struct Palm palm;
int need_slow_sync;

struct cmd_opts global_opts;	/* Command-line options */
struct config config;		/* Main configuration */

int
main(int argc, char *argv[])
{
	struct PConnection *pconn;
	int err;
	int i;

	MISC_TRACE(1)
		/* So I'll know what people are running when they send me
		 * stderr.
		 */
		print_version();

#if HAVE_LIBINTL
	/* Set things up so that i18n works. The constants PACKAGE and
	 * LOCALEDIR are strings set up in ../config.h by 'configure'.
	 */
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
#endif	/* HAVE_LIBINTL */

	/* Parse arguments and read config file(s) */
	if ((err = get_config(argc, argv)) < 0)
	{	
		fprintf(stderr, _("Error loading configuration\n"));
		exit(1);
	}

	MISC_TRACE(2)
	{
		fprintf(stderr, "Options:\n");
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
		fprintf(stderr, "\tMISC:\t%d\n", misc_trace);
	}

	/* Make sure at least one port was specified */
	if (config.listen == NULL)
	{
		fprintf(stderr, _("Error: no port specified.\n"));
		exit(1);
	}

	/* XXX - In the production version (daemon), this should just set
	 * up the serial port normally (raw, 9600 bps), then wait for it to
	 * become readable. Then fork, and let the child establish the
	 * connection and sync.
	 */
	/* XXX - If we're listening on a serial port, figure out fastest
	 * speed at which it will run.
	 */
	SYNC_TRACE(2)
		fprintf(stderr, "Opening device [%s]\n",
			config.listen[0].device);
	if ((pconn = new_PConnection(config.listen[0].device,
				     config.listen[0].listen_type)) == NULL)
	{
		fprintf(stderr, _("Error: can't open connection.\n"));
		exit(1);
	}

	printf(_("Please press the HotSync button.\n"));
				/* XXX - Don't print this in daemon mode */

	/* Connect to the Palm */
	if ((err = Connect(pconn)) < 0)
	{
		fprintf(stderr, _("Can't connect to Palm\n"));
		PConnClose(pconn);
		exit(1);
	}

	/* Get system, NetSync and user info */
	if ((err = GetPalmInfo(pconn, &palm)) < 0)
	{
		fprintf(stderr, _("Can't get system/user/NetSync info\n"));
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
				/* XXX - I'm not sure this is quite right.
				 * That is, if things are so screwed up
				 * that we can't get the user info, then
				 * I'm not sure that we can abort the
				 * connection cleanly.
				 */
		pconn = NULL;
		exit(1);
	}

	/* Figure out which Palm we're dealing with, and load per-palm
	 * config.
	 */
	if ((err = load_palm_config(&palm)) < 0)
	{
		fprintf(stderr, _("Can't get per-Palm config.\n"));
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
		pconn = NULL;
		exit(1);
	}

	/* XXX - In daemon mode, presumably load_palm_config() (or
	 * something) should tell us which user to run as. Therefore fork()
	 * an instance, have it setuid() to the appropriate user, and load
	 * that user's configuration.
	 */

	/* Initialize (per-user) conduits */
	MISC_TRACE(1)
		fprintf(stderr, "Initializing conduits\n");

	if ((err = init_conduits(&palm)) < 0)
	{
		fprintf(stderr, _("Can't initialize conduits\n"));
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
		pconn = NULL;
		exit(1);
	}

	if ((err = GetMemInfo(pconn, &palm)) < 0)
	{
		fprintf(stderr, _("GetMemInfo() returned %d\n"), err);
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
		pconn = NULL;
		exit(1);
	}

	/* Find out whether we need to do a slow sync or not */
	/* XXX - Actually, it's not as simple as this (see comment below) */
	if (hostid == palm.userinfo.lastsyncPC)
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

	/* Get a list of all databases on the Palm */
	if ((err = ListDBs(pconn, &palm)) < 0)
	{
		fprintf(stderr, _("ListDBs returned %d\n"), err);
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
		pconn = NULL;
		exit(1);
	}

	/* Print out the list of databases, for posterity */
	SYNC_TRACE(2)
	{
		fprintf(stderr, "\nDatabase list:\n");
		fprintf(stderr,
			"Name                            flags type crea ver mod. num\n"
			"        ctime                mtime                baktime\n");
		for (i = 0; i < palm.num_dbs; i++)
		{
			fprintf(stderr,
				"%-*s %04x %c%c%c%c %c%c%c%c %3d %08lx\n",
				PDB_DBNAMELEN,
				palm.dblist[i].name,
				palm.dblist[i].db_flags,
				(char) (palm.dblist[i].type >> 24),
				(char) (palm.dblist[i].type >> 16),
				(char) (palm.dblist[i].type >> 8),
				(char) palm.dblist[i].type,
				(char) (palm.dblist[i].creator >> 24),
				(char) (palm.dblist[i].creator >> 16),
				(char) (palm.dblist[i].creator >> 8),
				(char) palm.dblist[i].creator,
				palm.dblist[i].version,
				palm.dblist[i].modnum);
			fprintf(stderr, "        "
				"%02d:%02d:%02d %02d/%02d/%02d  "
				"%02d:%02d:%02d %02d/%02d/%02d  "
				"%02d:%02d:%02d %02d/%02d/%02d\n",
				palm.dblist[i].ctime.hour,
				palm.dblist[i].ctime.minute,
				palm.dblist[i].ctime.second,
				palm.dblist[i].ctime.day,
				palm.dblist[i].ctime.month,
				palm.dblist[i].ctime.year,
				palm.dblist[i].mtime.hour,
				palm.dblist[i].mtime.minute,
				palm.dblist[i].mtime.second,
				palm.dblist[i].mtime.day,
				palm.dblist[i].mtime.month,
				palm.dblist[i].mtime.year,
				palm.dblist[i].baktime.hour,
				palm.dblist[i].baktime.minute,
				palm.dblist[i].baktime.second,
				palm.dblist[i].baktime.day,
				palm.dblist[i].baktime.month,
				palm.dblist[i].baktime.year);
		}
	}

	/* Figure out what to do: backup, restore, or sync */
	/* XXX - It'd probably be a good idea to break this up into several
	 * functions. That is, add a "Sync()" function.
	 */
	if (global_opts.do_backup)
	{
		MISC_TRACE(1)
			fprintf(stderr, "Doing a backup to %s\n",
				global_opts.backupdir);
		err = Backup(pconn, &palm);
		MISC_TRACE(2)
			fprintf(stderr, "Backup() returned %d\n", err);
	} else if (global_opts.do_restore) {
		MISC_TRACE(1)
			fprintf(stderr, "Restoring from %s\n",
				global_opts.restoredir);
		err = Restore(pconn, &palm);
		MISC_TRACE(2)
			fprintf(stderr, "Restore() returned %d\n", err);
	} else {
		MISC_TRACE(1)
			fprintf(stderr, "Doing a sync.\n");

		/* XXX - Make sure that all of the relevant directories
		 * exist: ~/.palm/{backup,backup/Attic,archive,install}
		 */

		/* Install new databases */
		/* XXX - It should be configurable whether new databases
		 * get installed at the beginning or the end.
		 */
		err = InstallNewFiles(pconn, &palm, installdir, True);

		/* XXX - It should be possible to specify a list of
		 * directories to look in: that way, the user can put new
		 * databases in ~/.palm/install, whereas in a larger site,
		 * the sysadmin can install databases in /usr/local/stuff;
		 * they'll be uploaded from there, but not deleted.
		 */
		/* err = InstallNewFiles(pconn, &palm, "/tmp/palm-install",
				      False);*/
		if (err < 0)
		{
			fprintf(stderr, _("Error installing new files.\n"));
			Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
			pconn = NULL;
			exit(1);
		}

		/* For each database, walk config.fetch, looking for
		 * applicable conduits for each database.
		 */
		/* XXX - This should be redone: run_Fetch_conduits should
		 * be run _before_ opening the device. That way, they can
		 * be run even if we don't intend to sync with an actual
		 * Palm. Presumably, the Right Thing is to run the
		 * appropriate Fetch conduit for each file in
		 * ~/.palm/backup.
		 * OTOH, if you do it this way, then things won't work
		 * right the first time you sync: say you have a
		 * .coldsyncrc that has pre-fetch and post-dump conduits to
		 * sync with the 'kab' addressbook; you're syncing for the
		 * first time, so there's no ~/.palm/backup/AddressDB.pdb.
		 * In this case, the pre-fetch conduit doesn't get run, and
		 * the post-dump conduit overwrites the existing 'kab'
		 * database.
		 * Perhaps do things this way: run the pre-fetch conduits
		 * for the databases in ~/.palm/backup. Then, during the
		 * main sync, if a new database needs to be created on the
		 * workstation, run its pre-fetch conduit. (Or maybe this
		 * would be a good time to run the install conduit.)
		 */
		for (i = 0; i < palm.num_dbs; i++)
		{
			err = run_Fetch_conduits(&(palm.dblist[i]));
			if (err < 0)
			{
				fprintf(stderr, _("Error %d running pre-fetch "
						  "conduits.\n"),
					err);
				Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
				pconn = NULL;
				exit(1);
			}
		}

		/* Synchronize the databases */
		for (i = 0; i < palm.num_dbs; i++)
		{
			err = HandleDB(pconn, &palm, i);
			if (err < 0)
			{
				fprintf(stderr,
					_("!!! Oh, my God! A conduit failed! "
					  "Mayday, mayday! Bailing!\n"));
				/* XXX - Ought to be able to recover from
				 * this: if it's a problem with the conduit
				 * or with the local copy of the backup
				 * database, just print an error message
				 * and go on to the next one.
				 */
				Disconnect(pconn, DLPCMD_SYNCEND_OTHER);
				pconn = NULL;
				exit(1);
			}
		}

		/* XXX - If it's configured to install new databases last,
		 * install new databases now.
		 */

		/* Get list of local files: if there are any that aren't on
		 * the Palm, it probably means that they existed once, but
		 * were deleted on the Palm. Assuming that the user knew
		 * what he was doing, these databases should be deleted.
		 * However, just in case, they should be saved to an
		 * "attic" directory.
		 */
		err = CheckLocalFiles(&palm);

		/* XXX - Write updated NetSync info */
		/* Write updated user info */
		if ((err = UpdateUserInfo(pconn, &palm, 1)) < 0)
		{
			fprintf(stderr, _("Error writing user info\n"));
			Disconnect(pconn, DLPCMD_SYNCEND_OTHER);
			pconn = NULL;
			exit(1);
		}
	}

	if (synclog != NULL)
	{
		SYNC_TRACE(2)
			fprintf(stderr, "Writing log to Palm\n");

		if ((err = DlpAddSyncLogEntry(pconn, synclog)) < 0)
		{
			fprintf(stderr, _("Error writing sync log.\n"));
			Disconnect(pconn, DLPCMD_SYNCEND_OTHER);
			pconn = NULL;
			exit(1);
		}
	}

	/* Finally, close the connection */
	SYNC_TRACE(3)
		fprintf(stderr, "Closing connection to Palm\n");
	if ((err = Disconnect(pconn, DLPCMD_SYNCEND_NORMAL)) < 0)
	{
		fprintf(stderr, _("Error disconnecting\n"));
		exit(1);
	}
	pconn = NULL;

	/* XXX - The post-dump conduits should only be run if we're
	 * actually doing a sync. Is there a better way of expressing this?
	 */
	if ((!global_opts.do_backup) &&
	    (!global_opts.do_restore))
	{
		/* Run the post-dump conduits. */
		for (i = 0; i < palm.num_dbs; i++)
		{
			err = run_Dump_conduits(&(palm.dblist[i]));
			if (err < 0)
			{
				fprintf(stderr,
					_("Error %d running post-dump"
					  " conduits.\n"),
					err);
				break;
			}
		}
	}
	/* XXX - Clean up conduits */
	/* XXX - Is this still current, or is that left over from the old
	 * conduit API?
	 */
	MISC_TRACE(3)
		fprintf(stderr, "Cleaning up conduits\n");
	if ((err = tini_conduits()) < 0)
	{
		fprintf(stderr, _("Error cleaning up conduits\n"));
		exit(1);
	}

	MISC_TRACE(1)
		fprintf(stderr, "ColdSync terminating normally\n");
	exit(0);

	/* NOTREACHED */
}

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

	/* Compose a reply */
	cmpp.type = CMP_TYPE_INIT;
	cmpp.ver_major = 1;	/* XXX - Should be constants in header file */
	cmpp.ver_minor = 1;
	cmpp.rate = SYNC_RATE;
	cmpp.flags = CMP_IFLAG_CHANGERATE;

	SYNC_TRACE(5)
		fprintf(stderr, "===== Sending INIT packet\n");
	cmp_write(pconn, &cmpp);	/* XXX - Error-checking */

	SYNC_TRACE(5)
		fprintf(stderr, "===== Finished sending INIT packet\n");

	/* Change the speed */
	/* XXX - This probably goes in Pconn_accept() or something */

	if ((err = (*pconn->io_setspeed)(pconn, BSYNC_RATE)) < 0)
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

/* GetMemInfo
 * Get info about the memory on the Palm and record it in 'palm'.
 */
int
GetMemInfo(struct PConnection *pconn,
		struct Palm *palm)
{
	int err;
	ubyte last_card;
	ubyte more;

	/* Allocate space for the card info */
	if ((palm->cardinfo = (struct dlp_cardinfo *)
	     malloc(sizeof(struct dlp_cardinfo))) == NULL)
		return -1;

	/* Ask the Palm about each memory card in turn */
	/* XXX - Actually, it doesn't: it just asks about memory card 0;
	 * the 'more' return value should be non-zero if there are more
	 * cards to be read, but it's always one on every Palm I've tried
	 * this on.
	 */
	if ((err = DlpReadStorageInfo(pconn, CARD0, &last_card, &more,
				      palm->cardinfo)) < 0)
		return -1;

	palm->num_cards = 1;	/* XXX - Hard-wired, for the reasons above */

	MISC_TRACE(4)
	{
		fprintf(stderr, "===== Got memory info:\n");
		fprintf(stderr, "\tTotal size:\t%d\n",
			palm->cardinfo[0].totalsize);
		fprintf(stderr, "\tCard number:\t%d\n",
			palm->cardinfo[0].cardno);
		fprintf(stderr, "\tCard version: %d (0x%02x)\n",
			palm->cardinfo[0].cardversion,
			palm->cardinfo[0].cardversion);
		fprintf(stderr, "\tCreation time: %02d:%02d:%02d %d/%d/%d\n",
			palm->cardinfo[0].ctime.second,
			palm->cardinfo[0].ctime.minute,
			palm->cardinfo[0].ctime.hour,
			palm->cardinfo[0].ctime.day,
			palm->cardinfo[0].ctime.month,
			palm->cardinfo[0].ctime.year);
		fprintf(stderr, "\tROM: %ld (0x%04lx)\n",
			palm->cardinfo[0].rom_size,
			palm->cardinfo[0].rom_size);
		fprintf(stderr, "\tRAM: %ld (0x%04lx)\n",
			palm->cardinfo[0].ram_size,
			palm->cardinfo[0].ram_size);
		fprintf(stderr, "\tFree RAM: %ld (0x%04lx)\n",
			palm->cardinfo[0].free_ram,
			palm->cardinfo[0].free_ram);
		fprintf(stderr, "\tCard name (%d) \"%s\"\n",
			palm->cardinfo[0].cardname_size,
			palm->cardinfo[0].cardname);
		fprintf(stderr, "\tManufacturer name (%d) \"%s\"\n",
			palm->cardinfo[0].manufname_size,
			palm->cardinfo[0].manufname);

		fprintf(stderr, "\tROM databases: %d\n",
			palm->cardinfo[0].rom_dbs);
		fprintf(stderr, "\tRAM databases: %d\n",
			palm->cardinfo[0].ram_dbs);
	}

	/* XXX - Check DLP version */

	return 0;
}

/* ListDBs
 * Fetch the list of database info records from the Palm, both for ROM and
 * RAM.
 * This function must be called after GetMemInfo().
 */
int
ListDBs(struct PConnection *pconn, struct Palm *palm)
{
	int err;
	int card;		/* Memory card number */

	/* Iterate over each memory card */
	for (card = 0; card < palm->num_cards; card++)
	{
		int i;
		ubyte iflags;		/* ReadDBList flags */
		uword start;		/* Database index to start reading
					 * at */
		uword last_index;	/* Index of last database read */
		ubyte oflags;
		ubyte num;

		/* Get total # of databases */
		palm->num_dbs =
			palm->cardinfo[card].ram_dbs;
		if (global_opts.check_ROM)	/* Also considering ROM */
			palm->num_dbs += palm->cardinfo[card].rom_dbs;
		if (palm->num_dbs <= 0)
		{
			/* XXX - Fix this */
			fprintf(stderr,
				_("### Error: you have an old Palm, one that doesn't say how many\n"
				"databases it has. I can't cope with this.\n"));
			return -1;
		}

		/* Allocate space for the array of database info blocks */
		if ((palm->dblist = (struct dlp_dbinfo *)
		     calloc(palm->num_dbs, sizeof(struct dlp_dbinfo)))
		    == NULL)
			return -1;

		/* XXX - If the Palm uses DLP >= 1.2, get multiple
		 * databases at once.
		 */
		iflags = DLPCMD_READDBLFLAG_RAM;
		if (global_opts.check_ROM)
			iflags |= DLPCMD_READDBLFLAG_ROM;
				/* Flags: read at least RAM databases. If
				 * we're even considering the ROM ones,
				 * grab those, too.
				 */

		start = 0;	/* Index at which to start reading */

		/* Collect each database in turn. */
		/* XXX - Should handle older devices that don't return the
		 * number of databases. The easiest thing to do in this
		 * case is probably to resize palm->dblist dynamically
		 * (realloc()) as necessary. In this case, preallocating
		 * its size above becomes just an optimization for the
		 * special case where DLP >= 1.1.
		 */
		for (i = 0; i < palm->num_dbs; i++)
		{
			err = DlpReadDBList(pconn, iflags, card, start,
					    &last_index, &oflags, &num,
					    &(palm->dblist[i]));
			/* XXX - Until we can get the DLP status code from
			 * palm_errno, we'll need to ignore 'err'.
			 */
			if (err < 0)
				return -1;

			/* Sanity check: if there are no more databases to
			 * be read, stop reading now. This shouldn't
			 * happen, but you never know.
			 */
			if ((oflags & 0x80) == 0)	/* XXX - Need const */
				/* There are no more databases */
				break;

			/* For the next iteration, set the start index to
			 * the index of the database just read, plus one.
			 */
			start = last_index + 1;
			start = last_index+1; 
		}
	}

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
		char *lastdot;		/* Pointer to last dot in filename */
		struct dlp_dbinfo *db;
		static char dbname[DLPCMD_DBNAME_LEN+1];
		static char fromname[MAXPATHLEN+1];
					/* Full pathname of the database */
		static char toname[MAXPATHLEN+1];
					/* Pathname to which we'll move the
					 * database.
					 */
		struct stat statbuf;	/* Used to see if a file exists */
		int n;


		MISC_TRACE(4)
			fprintf(stderr,
				"CheckLocalFiles: Checking file \"%s\"\n",
				file->d_name);

		/* Check the extension, if any, on the filename.
		 * The only files we care about here are those that end in
		 * ".pdb" or ".prc". Anything other than these files is
		 * unlikely to be taken for a database and erroneously
		 * uploaded or something.
		 */
		lastdot = strrchr(file->d_name, '.');
		if (lastdot == NULL)
			/* No extension. Ignore this file */
			continue;

		if ((strcasecmp(lastdot, ".pdb") != 0) &&
		    (strcasecmp(lastdot, ".prc") != 0))
			/* The file doesn't have a database extension.
			 * Ignore it.
			 */
			continue;

		MISC_TRACE(5)
			fprintf(stderr,
				"Found a database: \"%s\"\n",
				file->d_name);

		/* See if a database by this name exists on the Palm. */

		/* Extract the database name. Try not to overflow the
		 * buffer
		 */
		if ((lastdot - file->d_name) >= DLPCMD_DBNAME_LEN)
		{
			strncpy(dbname, file->d_name,
				DLPCMD_DBNAME_LEN);
			dbname[DLPCMD_DBNAME_LEN] = '\0';
		} else {
			strncpy(dbname, file->d_name,
				lastdot - file->d_name);
			dbname[lastdot - file->d_name] = '\0';
		}

		/* See if this database exists on the Palm */
		MISC_TRACE(4)
			fprintf(stderr,
				"Checking for \"%s\" on the Palm\n",
				dbname);
		if ((db = find_dbentry(palm, dbname)) != NULL)
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
		err = lstat(toname, &statbuf);
		if (err < 0)
		{
			/* If stat() failed because the file doesn't exist,
			 * that's good. Anything else is a genuine error,
			 * and is bad.
			 */
			if (errno != ENOENT)
			{
				fprintf(stderr, _("%s: Error in checking for "
						  "\"%s\"\n"),
					"CheckLocalFiles",
					toname);
				perror("stat");
				closedir(dir);
				return -1;
			}

			/* stat() failed because the file doesn't exist.
			 * Move the database to the attic.
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
			err = lstat(toname, &statbuf);
			if (err == 0)
				/* The file exists */
				continue;

			/* If stat() failed because the file doesn't exist,
			 * that's good. Anything else is a genuine error,
			 * and is bad.
			 */
			if (errno != ENOENT)
			{
				fprintf(stderr, _("%s: Error in checking for "
						  "\"%s\"\n"),
					"CheckLocalFiles",
					toname);
				perror("stat");
				closedir(dir);
				return -1;
			}

			/* stat() failed because the file doesn't exist.
			 * Move the database to the attic.
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

/* GetUserSysInfo
 * Get system and user info from the Palm, and put it in 'palm'.
 */
int
GetPalmInfo(struct PConnection *pconn,
	       struct Palm *palm)
{
	int err;

	/* Get system information about the Palm */
	if ((err = DlpReadSysInfo(pconn, &(palm->sysinfo))) < 0)
	{
		fprintf(stderr, _("Can't get system info\n"));
		return -1;
	}
	MISC_TRACE(3)
	{
		fprintf(stderr, "System info:\n");
		fprintf(stderr, "\tROM version: 0x%08lx\n",
			palm->sysinfo.rom_version);
		fprintf(stderr, "\tLocalization: 0x%08lx\n",
			palm->sysinfo.localization);
		fprintf(stderr, "\tproduct ID: 0x%08lx\n",
			palm->sysinfo.prodID);
	}

	/* Get NetSync information from the Palm */
	/* XXX - Need to check some stuff, first: if it's a pre-1.1 Palm,
	 * it doesn't know about NetSync. HotSync tries to
	 * OpenDB("NetSync"), then it tries ReadFeature('netl',0), before
	 * calling ReadNetSyncInfo().
	 */
	err = DlpReadNetSyncInfo(pconn, &(palm->netsyncinfo));
	switch (err)
	{
	    case DLPSTAT_NOERR:
		MISC_TRACE(3)
		{
			fprintf(stderr, "NetSync info:\n");
			fprintf(stderr, "\tLAN sync on: %d\n",
				palm->netsyncinfo.lansync_on);
			fprintf(stderr, "\thostname: \"%s\"\n",
				palm->netsyncinfo.hostname);
			fprintf(stderr, "\thostaddr: \"%s\"\n",
				palm->netsyncinfo.hostaddr);
			fprintf(stderr, "\tnetmask: \"%s\"\n",
				palm->netsyncinfo.hostnetmask);
		}
		break;
	    case DLPSTAT_NOTFOUND:
		printf(_("No NetSync info.\n"));
		break;
	    default:
		fprintf(stderr, _("Error reading NetSync info\n"));
		return -1;
	}

	/* Get user information from the Palm */
	if ((err = DlpReadUserInfo(pconn, &(palm->userinfo))) < 0)
	{
		fprintf(stderr, _("Can't get user info\n"));
		return -1;
	}
	MISC_TRACE(3)
	{
		fprintf(stderr, "User info:\n");
		fprintf(stderr, "\tUserID: 0x%08lx\n", palm->userinfo.userid);
		fprintf(stderr, "\tViewerID: 0x%08lx\n",
			palm->userinfo.viewerid);
		fprintf(stderr, "\tLast sync PC: 0x%08lx (%d.%d.%d.%d)\n",
			palm->userinfo.lastsyncPC,
			(int) ((palm->userinfo.lastsyncPC >> 24) & 0xff),
			(int) ((palm->userinfo.lastsyncPC >> 16) & 0xff),
			(int) ((palm->userinfo.lastsyncPC >>  8) & 0xff),
			(int) (palm->userinfo.lastsyncPC & 0xff));
		if (palm->userinfo.lastgoodsync.year == 0)
		{
			fprintf(stderr, "\tLast good sync: never\n");
		} else {
			fprintf(stderr, "\tLast good sync: %02d:%02d:%02d "
				"%02d/%02d/%02d\n",
				palm->userinfo.lastgoodsync.hour,
				palm->userinfo.lastgoodsync.minute,
				palm->userinfo.lastgoodsync.second,
				palm->userinfo.lastgoodsync.day,
				palm->userinfo.lastgoodsync.month,
				palm->userinfo.lastgoodsync.year);
		}
		if (palm->userinfo.lastsync.year == 0)
		{
			fprintf(stderr, "\tLast sync attempt: never\n");
		} else {
			fprintf(stderr,
				"\tLast sync attempt: %02d:%02d:%02d "
				"%02d/%02d/%02d\n",
				palm->userinfo.lastsync.hour,
				palm->userinfo.lastsync.minute,
				palm->userinfo.lastsync.second,
				palm->userinfo.lastsync.day,
				palm->userinfo.lastsync.month,
				palm->userinfo.lastsync.year);
		}
		fprintf(stderr, "\tUser name length: %d\n",
			palm->userinfo.usernamelen);
		fprintf(stderr, "\tUser name: \"%s\"\n",
			palm->userinfo.username == NULL ?
			"(null)" : palm->userinfo.username);
		fprintf(stderr, "\tPassword: <%d bytes>\n",
			palm->userinfo.passwdlen);
		MISC_TRACE(6)
			debug_dump(stderr, "PASS", palm->userinfo.passwd,
				   palm->userinfo.passwdlen);
	}

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
		fprintf(stderr, "* UpdateUserInfo:\n");

	/* Does this Palm have a user ID yet? */
	/* XXX - If the Palm has a user ID, but it's not that of the
	 * current user, it should be possible to overwrite it. Perhaps
	 * this should be an "administrative mode" option.
	 */
	if (palm->userinfo.userid == 0)
	{
		MISC_TRACE(3)
			fprintf(stderr, "Setting UID to %d (0x%04x)\n",
				(int) user_uid,
				(unsigned int) user_uid);
		/* XXX - Fill this in */
		uinfo.userid = (udword) user_uid;
		uinfo.modflags |= DLPCMD_MODUIFLAG_USERID;
					/* Set modification flag */
	}

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
/*  fprintf(stderr, "Trying %ld bps\n", speeds[i].bps); */
		if ((*pconn->io_setspeed)(pconn, speeds[i].tcspeed) == 0)
			return i;
/*  fprintf(stderr, "Nope\n"); */
	}

/*  fprintf(stderr, "Couldn't find a suitable speed.\n"); */
	return -1;
}

/* set_debug_level
 * Set a debugging/trace value. These are settable on a per-facility basis
 * (see struct debug_flags in "coldsync.h"). Thus, specifying
 * "-d SLP:1 -d PADP:10" will give the barest minimum of information as to
 * what is happening in the SLP layer, but will spew gobs of information
 * about the PADP layer, including dumps of every packet going back and
 * forth.
 * Note to hackers: as a general rule, a debugging level of 0 means no
 * debugging messages (this is the default). 1 means give the user some
 * idea of what is going on. 5 means print a message for every packet; 6
 * means print the contents of every packet. 10 means... well, let's just
 * not think about that, shall we?
 * These numbers are just guidelines, though. Your circumstances will vary.
 *
 * 'str' can take on one of the following
 * forms:
 *	FAC	Set facility FAC to level 1
 *	FAC:N	Set facility FAC to level N
 */
void
set_debug_level(const char *str)
{
	const char *lvlstr;	/* Pointer to trace level in 'str' */
	int lvl;		/* Trace level */

	/* See if 'str' contains a colon */
	if ((lvlstr = strchr(str, ':')) != NULL)
	{
		/* 'str' contains a colon. Parse the string */
		lvlstr++;		/* Make 'lvlstr' point to the level
					 * number */
		lvl = atoi(lvlstr);
	} else {
		/* 'str' does not contain a colon. Set the trace level to 1 */
		lvl = 1;
	}

	/* Set the appropriate debugging facility. */
	if (strncasecmp(str, "slp:", 4) == 0)
		slp_trace = lvl;
	else if (strncasecmp(str, "cmp:", 4) == 0)
		cmp_trace = lvl;
	else if (strncasecmp(str, "padp:", 5) == 0)
		padp_trace = lvl;
	else if (strncasecmp(str, "dlpc:", 5) == 0)
		dlpc_trace = lvl;
	else if (strncasecmp(str, "dlp:", 4) == 0)
		dlp_trace = lvl;
	else if (strncasecmp(str, "sync:", 5) == 0)
		sync_trace = lvl;
	else if (strncasecmp(str, "pdb:", 4) == 0)
		pdb_trace = lvl;
	else if (strncasecmp(str, "parse:", 6) == 0)
		parse_trace = lvl;
	else if (strncasecmp(str, "misc:", 5) == 0)
		misc_trace = lvl;
	else {
		fprintf(stderr, _("Unknown facility \"%s\"\n"), str);
	}
}

/* usage
 * Print out a usage string.
 * XXX - Move this to "config.c"
 */
/* ARGSUSED */
void
usage(int argc, char *argv[])
{
	printf(_("Usage: %s [options] -p port\n"
		 "Options:\n"
		 "\t-h:\t\tPrint this help message and exit.\n"
		 "\t-V:\t\tPrint version and exit.\n"
		 "\t-f <file>:\tRead configuration from <file>.\n"
		 "\t-b <dir>:\tPerform a backup to <dir>.\n"
		 "\t-r <dir>:\tRestore from <dir>.\n"
		 "\t-S:\t\tForce slow sync.\n"
		 "\t-F:\t\tForce fast sync.\n"
		 "\t-R:\t\tCheck ROM databases.\n"
		 "\t-p <port>:\tListen on device <port>\n"
		 "\t-d <fac[:level]>:\tSet debugging level.\n")
	       ,
	       argv[0]);
}

/* print_version
 * Print out the version of ColdSync.
 */
void
print_version(void)
{
	printf(_("%s version %s\n"),
	       /* These two strings are defined in "config.h" */
	       PACKAGE,
	       VERSION);
	/* XXX - Ought to print out other information, e.g., copyright,
	 * compile-time flags, optional packages, maybe OS name and
	 * version, who compiled it and when, etc.
	 */
	printf(_("Compile-type options:\n"));

#if WITH_USB
	printf(
_("    WITH_USB: USB support.\n"));
#endif	/* WITH_USB */
#if WITH_EFENCE
	printf(
_("    WITH_EFENCE: buffer overruns will cause a segmentation violation.\n"));
#endif	/* WITH_EFENCE */

#if HAVE_STRCASECMP && HAVE_STRNCASECMP
	printf(
_("    HAVE_STRCASECMP, HAVE_STRNCASECMP: strings are compared without regard\n"
"        to case, whenever possible.\n"));
#endif	/* HAVE_STRCASECMP && HAVE_STRNCASECMP */
/* XXX */
	printf(
_("\n"
"    Default global configuration file: %s\n"),
		DEFAULT_GLOBAL_CONFIG);
}

/* find_dbentry
 * XXX - This really doesn't belong in this file.
 * Look through the list of databases in 'palm' and try to find the one
 * named 'name'. Returns a pointer to its entry in 'palm->dblist' if it
 * exists, or NULL otherwise.
 */
struct dlp_dbinfo *
find_dbentry(struct Palm *palm,
	     const char *name)
{
	int i;

	if (palm == NULL)
		return NULL;		/* Paranoia */

	for (i = 0; i < palm->num_dbs; i++)
	{
		if (strncmp(name, palm->dblist[i].name,
			    DLPCMD_DBNAME_LEN) == 0)
			/* Found it */
			return &(palm->dblist[i]);
	}

	return NULL;		/* Couldn't find it */
}

/* append_dbentry
 * Append an entry for 'pdb' to palm->dblist.
 */
int
append_dbentry(struct Palm *palm,
	       struct pdb *pdb)
{
	struct dlp_dbinfo *newdblist;
	struct dlp_dbinfo *dbinfo;

	MISC_TRACE(4)
		fprintf(stderr, "append_dbentry: adding \"%s\"\n",
			pdb->name);

	/* Resize the existing 'dblist'. */
	newdblist = realloc(palm->dblist,
			    (palm->num_dbs+1) * sizeof(struct dlp_dbinfo));
	if (newdblist == NULL)
	{
		fprintf(stderr, _("Error resizing palm->dblist\n"));
		return -1;
	}
	palm->dblist = newdblist;
	palm->num_dbs++;

	/* Fill in the new entry */
	dbinfo = &(palm->dblist[palm->num_dbs-1]);
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

/* This is for Emacs's benefit:
 * Local Variables:	***
 * fill-column:	75	***
 * End:			***
 */
