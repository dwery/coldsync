/* sync.c
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: sync.c,v 2.2 2002-08-31 19:26:03 azummo Exp $
 */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>		/* For malloc(), atoi() */
#include <fcntl.h>		/* For open() */
#include <sys/param.h>		/* For MAXPATHLEN */
#include <termios.h>		/* Experimental */
#include <dirent.h>		/* For opendir(), readdir(), closedir() */
#include <string.h>		/* For strrchr() */
#include <netdb.h>		/* For gethostbyname2() */
#include <sys/socket.h>		/* For AF_* */
#include <netinet/in.h>		/* For in_addr */
#include <arpa/inet.h>		/* For inet_ntop() and friends */

#if HAVE_STRINGS_H
#  include <strings.h>		/* For strcasecmp() under AIX */
#endif	/* HAVE_STRINGS_H */

#if HAVE_INET_NTOP
#  include <arpa/nameser.h>	/* Solaris's <resolv.h> requires this */
#  include <resolv.h>		/* For inet_ntop() under Solaris */
#endif	/* HAVE_INET_NTOP */
#include <unistd.h>		/* For sleep() */
#include <ctype.h>		/* For isalpha() and friends */
#include <errno.h>		/* For errno. Duh. */
#include <time.h>		/* For ctime() */
#include <syslog.h>		/* For syslog() */
#include <pwd.h>		/* For getpwent() */

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
#include "palment.h"
#include "symboltable.h"
#include "palmconn.h"
#include "netsync.h"

extern struct pref_item *pref_cache;

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
static int
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
		Error(_("%s: Can't open directory \"%s\"."),
		      "CheckLocalFiles",
		      backupdir);
		Perror("opendir");
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
		snprintf(fromname, MAXPATHLEN,
			 "%s/%s", backupdir, file->d_name);

		/* First try at a destination pathname:
		 * <atticdir>/<filename>.
		 */
		snprintf(toname, MAXPATHLEN,
			 "%s/%s", atticdir, file->d_name);

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
				Error(_("%s: Can't rename \"%s\" to "
					"\"%s\"."),
				      "CheckLocalFiles",
				      fromname, toname);
				Perror("rename");
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
			/* Construct the proposed destination name:
			 * <atticdir>/<filename>~<n>
			 */
			snprintf(toname, MAXPATHLEN,
				 "%s/%s~%d", atticdir, file->d_name, n);

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
				Error(_("%s: Can't rename \"%s\" to "
					"\"%s\"."),
				      "CheckLocalFiles",
				      fromname, toname);
				Perror("rename");
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
			Error(_("Too many files named \"%s\" in the attic."),
			      file->d_name);
		}
	}
	closedir(dir);

	return 0;
}


/* Updates the Palm's user info. Requires an initialized dlp_setuserinfo
 * structure.
 */

int
UpdateUserInfo2(struct Palm *palm, struct dlp_setuserinfo *newinfo)
{
	MISC_TRACE(1)
		fprintf(stderr, "Updating user info.\n");

	newinfo->modflags = 0;

	if (palm_userid(palm) != newinfo->userid)
	{
		MISC_TRACE(3)
			fprintf(stderr, "Setting UserID to %d (0x%04x)\n",
				(int) newinfo->userid,
				(unsigned int) newinfo->userid);

		newinfo->modflags |= DLPCMD_MODUIFLAG_USERID;
					/* Set modification flag */

		va_add_to_log(palm_pconn(palm), "%s: %lu\n",
			      _("Set user ID"), newinfo->userid);

	}

	/* Fill in the user name if there isn't one or if it has changed
	 */

	if ((newinfo->username != NULL) &&
		( !palm_has_username(palm) || (strcmp(palm_username(palm), newinfo->username) != 0)))
	{
		MISC_TRACE(3)
			fprintf(stderr, "Setting user name to \"%s\"\n",
				newinfo->username);

		newinfo->usernamelen	= strlen(newinfo->username)+1;
		newinfo->modflags	|= DLPCMD_MODUIFLAG_USERNAME;

		MISC_TRACE(3)
			fprintf(stderr, "User name length == %d\n",
				newinfo->usernamelen);

		va_add_to_log(palm_pconn(palm), "%s: %s\n",
			      _("Set username"),
			      newinfo->username);
	}

	/* Check is the lastsyncPC is changed
	 */

	if (palm_lastsyncPC(palm) != newinfo->lastsyncPC)
	{
		/* Fill in this machine's host ID as the last sync PC */
		MISC_TRACE(3)
			fprintf(stderr, "Setting lastsyncPC to 0x%08lx\n", newinfo->lastsyncPC);

		newinfo->modflags |= DLPCMD_MODUIFLAG_SYNCPC;
	}


	/* Should we update lastsync date?
	 */

	if (newinfo->lastsync.year != 0)
	{
		/* Yes */
	
		MISC_TRACE(3)
			fprintf(stderr, "Setting lastsync date to %d\n",
				(int) time_dlp2time_t(&newinfo->lastsync));
	
		newinfo->modflags |= DLPCMD_MODUIFLAG_SYNCDATE;
	}

	/* Send the updated user info to the Palm */
	return DlpWriteUserInfo(palm_pconn(palm), newinfo);
}


/* UpdateUserInfo
 * Update the Palm's user info. 'success' indicates whether the sync was
 * successful.
 */
static int
UpdateUserInfo(const struct Palm *palm,
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
	err = DlpWriteUserInfo(palm_pconn(palm),
			       &uinfo);
	if (err != (int) DLPSTAT_NOERR)
	{
		Error(_("DlpWriteUserInfo failed."));
		print_latest_dlp_error(palm_pconn(palm));
		return -1;
	}

	return 0;		/* Success */
}


static int
conduits_install(struct Palm *palm, pda_block *pda)
{
	struct dlp_dbinfo dbinfo;
	int err;
	
	/* Run any install conduits on the dbs in install directory
	 * Notice that install conduits are *not* run on files
	 * named for install on the command line.
	 */
	Verbose(1, _("Running Install conduits"));

	/* Run "none" conduits */
 	err = run_Install_conduits(palm, NULL, pda);
 	if (err < 0)
 	{
 	 	Error(_("Error %d running install conduits."),
 	 	      err);
 	 	return -1;
 	}


	while (NextInstallFile(&dbinfo)>=0) {
		err = run_Install_conduits(palm, &dbinfo, pda);
		if (err < 0) {
			Error(_("Error %d running install "
				"conduits."),
			      err);
			return -1;
		}
	}

	err = InstallNewFiles(palm, installdir, True, global_opts.force_install);
	if (err < 0)
	{
		Error(_("Can't install new files."));
		return -1;
	}

	return 0;
}

static int
conduits_dump(struct Palm *palm, pda_block *pda)
{
	int err;
	const struct dlp_dbinfo *cur_db;
					/* Used when iterating over all
					 * databases */

	Verbose(1, _("Running Dump conduits"));

	/* Run "none" conduits */
 	err = run_Dump_conduits(palm, NULL, pda);
 	if (err < 0)
 	{
 	 	Error(_("Error %d running post-dump conduits."),
 	 	      err);
 	 	return -1;
 	}

	palm_resetdb(palm);

	while ((cur_db = palm_nextdb(palm)) != NULL)
	{
		err = run_Dump_conduits(palm, cur_db, pda);
		if (err < 0)
		{
			Error(_("Error %d running post-dump conduits."),
			      err);
			break;
		}
	}

	return 0;	
}

static int
conduits_fetch(struct Palm *palm, pda_block *pda)
{
	int err;
	const struct dlp_dbinfo *cur_db;
					/* Used when iterating over all
					 * databases */

	Verbose(1, _("Running Fetch conduits"));

	/* Run "none" conduits */
 	err = run_Fetch_conduits(palm, NULL, pda);
 	if (err < 0)
 	{
 	 	Error(_("Error %d running pre-fetch conduits."),
 	 	      err);
 	 	return -1;
 	}
 

	palm_resetdb(palm);

	while ((cur_db = palm_nextdb(palm)) != NULL)
	{
		err = run_Fetch_conduits(palm, cur_db, pda);
		if (err < 0)
		{
			Error(_("Error %d running pre-fetch conduits."),
			      err);
			return -1;
		}
	}
	
	return 0;
}

static int
conduits_sync(struct Palm *palm, pda_block *pda)
{
	int err;
	const struct dlp_dbinfo *cur_db;

	/* Synchronize the databases */
	Verbose(1, _("Running Sync conduits"));

	/* Run "none" conduits */
 	err = run_Sync_conduits(palm, NULL, pda);
 	if (err < 0)
 	{
 	 	Error(_("Error %d running sync conduits."),
 	 	      err);
 	 	return -1;
 	}


	palm_resetdb(palm);
	while ((cur_db = palm_nextdb(palm)) != NULL)
	{
		/* Run the Sync conduits for this database. This includes
		 * built-in conduits.
		 */
		Verbose(2, _("Syncing %s"), cur_db->name);

		err = run_Sync_conduits(palm, cur_db, pda);
		if (err < 0)
		{
			switch (cs_errno)
			{
			    case CSE_CANCEL:
			    case CSE_NOCONN:
				return -1;

			    default:
				Warn(_("Conduit failed for unknown "
				       "reason."));
				/* Continue, and hope for the best */
				continue;
			}
		}
	}

	return 0;
}

int
do_sync(pda_block *pda, struct Palm *palm)
{
	int err;
	struct pref_item *pref_cursor;

	udword p_lastsyncPC;		/* Hostid of last host Palm synced
					 * with */

	/* XXX - If the PDA block has forwarding turned on, then see which
	 * host to forward to (NULL == whatever the Palm wants). Check to
	 * see if we're that host. If so, continue normally. Otherwise,
	 * open a NetSync connection to that host and allow it to sync.
	 * Then return from run_mode_Standalone().
	 *
	 * How to figure out whether this is the host to sync with:
	 *	Get all of this host's addresses (get_hostaddrs()).
	 *	gethostbyname(remotehost) to get all of the remote host's
	 *	addresses.
	 *	Compare each local address with each remote address. If
	 *	there's a match, then localhost == remotehost.
	 * The way to get the list of the local host's addresses is
	 * ioctl(SIOCGIFCONF). See Stevens, UNP1, chap. 16.6.
	 */
	if ((pda != NULL) && pda->forward)
	{
		err = forward(pda, palm);

		/* The remote host will send the Palm the DlpEndOfSync
		 * command and the local connection gets closed.
		 * We must avoid any further communication if the sync
		 * succeeded.
		 */ 

		if (err == 0)
		{
			PConn_set_status(palm_pconn(palm), PCONNSTAT_CLOSED);
			return 0;
		}
		if (err < 0)
		{
			palm_Disconnect(palm, DLPCMD_SYNCEND_CANCEL);
			return -1;
		}
	}

	/* Figure out what the base sync directory is */
	if ((pda != NULL) && (pda->directory != NULL))
	{
		/* Use the directory specified in the config file */
		strncpy(palmdir, pda->directory, MAXPATHLEN);
	} else {
		/* Either there is no applicable PDA, or else it doesn't
		 * specify a directory. Use the default (~/.palm).
		 */
		strncpy(palmdir,
			mkfname(userinfo.homedir, "/.palm", NULL),
			MAXPATHLEN);
	}
	
	MISC_TRACE(3)
		fprintf(stderr, "Base directory is [%s]\n", palmdir);

	/* Make sure the sync directories exist */
	err = make_sync_dirs(palmdir);
	if (err < 0)
	{
		/* An error occurred while creating the sync directories */
		palm_Disconnect(palm, DLPCMD_SYNCEND_CANCEL);
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
		fprintf(stderr, "Initializing preference cache\n");

	if ((err = CacheFromConduits(sync_config->conduits, palm_pconn(palm))) < 0)
	{
		Error(_("CacheFromConduits() returned %d."), err);
		palm_Disconnect(palm, DLPCMD_SYNCEND_CANCEL);
		return -1;
	}

	/* Find out whether we need to do a slow sync or not */
	/* XXX - Actually, it's not as simple as this (see comment below) */
	p_lastsyncPC = palm_lastsyncPC(palm);
	if ((p_lastsyncPC == 0) && !palm_ok(palm))
	{
		Error(_("Can't get last sync PC from Palm"));
		palm_Disconnect(palm, DLPCMD_SYNCEND_CANCEL);
		return -1;
	}

	if (hostid == p_lastsyncPC)
		/* We synced with this same machine last time, so we can do
		 * a fast sync this time.
		 */
		need_slow_sync = 0;
	else
		/* The Palm synced with some other machine, the last time
		 * it synced. We need to do a slow sync.
		 */
		need_slow_sync = 1;

	/* Print verbose message here so it only gets printed once */
	if (global_opts.force_slow)
		Verbose(1, _("Doing a (forced) slow sync."));
	else if (global_opts.force_fast)
		Verbose(1, _("Doing a (forced) fast sync."));
	else if (need_slow_sync)
		Verbose(1, _("Doing a slow sync."));
	else
		Verbose(1, _("Doing a fast sync."));

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

	MISC_TRACE(1)
		fprintf(stderr, "Doing a sync.\n");

	if (sync_config->options.filter_dbs == True)	 
	{ 
		conduit_block *conduit;
	
		/* Walk the queue */
		for (conduit = sync_config->conduits;
		     conduit != NULL;
		     conduit = conduit->next)
		{
			int i;

			/* See if the flavor matches */
			if (!(conduit->flavors & FLAVORFL_SYNC))
			 	continue;

			/* Avoid default conduits */
			if (conduit->flags & CONDFL_DEFAULT)
				continue;

			for (i = 0; i < conduit->num_ctypes; i++)
			{
				/* Avoid having wildcards in both creator and type fields */
				if( conduit->ctypes[i].creator == 0 && conduit->ctypes[i].type == 0)
					continue;

				SYNC_TRACE(2)
				{
					fprintf(stderr,
						"Searching DBs:\n"
						"\ttype   : '%c%c%c%c' (0x%08lx)\n"
                                 	        "\tcreator: '%c%c%c%c' (0x%08lx)\n",
                                 	        (char) (conduit->ctypes[i].type >> 24) & 0xff,
                                 	        (char) (conduit->ctypes[i].type >> 16) & 0xff,
                                 	        (char) (conduit->ctypes[i].type >> 8) & 0xff, 
                                 	        (char) conduit->ctypes[i].type & 0xff,
                                 	        conduit->ctypes[i].type,
                                 	        (char) (conduit->ctypes[i].creator >> 24) & 0xff,
                                 	        (char) (conduit->ctypes[i].creator >> 16) & 0xff,
                                 	        (char) (conduit->ctypes[i].creator >> 8) & 0xff, 
                                 	        (char) conduit->ctypes[i].creator & 0xff,
						conduit->ctypes[i].creator);
				}

				palm_fetch_some_DBs(palm, conduit->ctypes[i].creator, conduit->ctypes[i].type);
			}
		}
	}
	else
	{	 
		err = palm_fetch_all_DBs(palm);	/* We're going to be looking at all
						 * of the databases on the Palm, so
						 * make sure we get them all.
						 */
			/* XXX - Off hand, it looks as if fetching the list
			 * of databases takes a long time (several
			 * seconds). One way to "fix" this would be to get
			 * each database in turn, then run its conduits.
			 * The problems with this are that a) it doesn't
			 * make things faster in the long run, and b) it
			 * screws up the sequence of events documented in
			 * the man page.
			 * If it were possible to set the display on the
			 * Palm to not just say "Identifying", it might
			 * make things _appear_ significantly faster.
			 */
		if (err < 0)
		{
			Error(_("Can't fetch list of databases."));
			/* print_cs_errno(cs_errno); */
			palm_CSDisconnect(palm);

			return -1;
		}
	}

	/* Install any file in the system wide "install" directory */
	InstallNewFiles(palm, GLOBAL_INSTALL_DIR, False, False);

	/* Install any file in the "rescue" directory */
	InstallNewFiles(palm, rescuedir, False, False);

	/* XXX - Do we need install conduits for the above directories? */

	/* Install new databases before sync, if the config says so */
	if (global_opts.install_first)
	{
		if ((err = conduits_install(palm, pda)) < 0)
		{
			palm_Disconnect(palm, DLPCMD_SYNCEND_CANCEL);
			return -1;
		}
	}

	/* XXX - It should be possible to specify a list of directories to
	 * look in: that way, the user can put new databases in
	 * ~/.palm/install, whereas in a larger site, the sysadmin can
	 * install databases in /usr/local/stuff; they'll be uploaded from
	 * there, but not deleted.
	 * E.g.:
	 *
	 * err = InstallNewFiles(palm, "/tmp/palm-install", False, False);
	 */

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

	if ((err = conduits_fetch(palm, pda)) < 0)
	{
		palm_Disconnect(palm, DLPCMD_SYNCEND_CANCEL);
		return -1;
	}

	if ((err = conduits_sync(palm, pda)) < 0)
	{
		/* print_cs_errno(cs_errno); */

		if (cs_errno == CSE_CANCEL)
		{
			va_add_to_log(palm_pconn(palm), _("*Cancelled*\n"));
				/* Doesn't really matter if it
				 * fails, since we're terminating
				 * anyway.
				 */
		}

		palm_CSDisconnect(palm);
		return -1;
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
	if (err < 0)
	{
		switch (cs_errno)
		{
		    case CSE_NOCONN:
		    	/* print_cs_errno(cs_errno); */
			palm_Disconnect(palm, DLPCMD_SYNCEND_OTHER);
			return -1;
		    default:
			/* Hope for the best */
			break;
		}
	}

	/* XXX - Write updated NetSync info */
	/* Write updated user info */
	if ((err = UpdateUserInfo(palm, 1)) < 0)
	{
		Error(_("Can't write user info."));
		palm_Disconnect(palm, DLPCMD_SYNCEND_OTHER);
		return -1;
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
		    palm_pconn(palm) == pref_cursor->pconn)
		{
			err = FetchPrefItem(palm_pconn(palm), pref_cursor);
			if (err < 0)
			{
				switch (cs_errno)
				{
				    case CSE_NOCONN:
					/* print_cs_errno(cs_errno); */
					palm_Disconnect(palm, DLPCMD_SYNCEND_OTHER);
					return -1;
				    default:
					Warn(_("Can't fetch preference "
					       "0x%08lx/%d."),
					     pref_cursor->description.creator,
					     pref_cursor->description.id);
					/* Continue and hope for the best */
					break;
				}
			}
		}
	}

	/* Install new databases after sync */
	if (!global_opts.install_first)
	{
		if ((err = conduits_install(palm, pda)) < 0)
		{
			palm_Disconnect(palm, DLPCMD_SYNCEND_CANCEL);
			return -1;
		}
	}

	/* Finally, close the connection */
	palm_Disconnect(palm, DLPCMD_SYNCEND_NORMAL);

	/* Run Dump conduits */
	err = conduits_dump(palm, pda);

	/* XXX - Is this check still needed ? */
	if (cs_errno == CSE_NOCONN)
	{
		/* print_cs_errno(cs_errno); */
		return -1;
	}

	return 0;
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


/* This is for Emacs's benefit:
 * Local Variables:	***
 * fill-column:	75	***
 * End:			***
 */
