/* runmode.c
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: runmode.c,v 2.5 2002-10-16 18:59:32 azummo Exp $
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
#include "net_compat.h"
#include "symboltable.h"
#include "netsync.h"
#include "palmconn.h"
#include "sync.h"


int
run_mode_Standalone(int argc, char *argv[])
{
	struct Palm *palm;
	pda_block *pda;			/* The PDA we're syncing with. */
	const char *p_username;		/* The username on the Palm */
	const char *want_username;	/* The username we expect to see on
					 * the Palm. */
	udword p_userid;		/* The userid on the Palm */
	udword want_userid;		/* The userid we expect to see on
					 * the Palm. */
	time_t now;

	/* Connect to the Palm */
	if( (palm = palm_Connect()) == NULL )
		return -1;

	/* Figure out which Palm we're dealing with */
	pda = find_pda_block(palm, True);
	if (pda == NULL && !palm_ok(palm))
	{
		/* Error-checking (not to be confused with "pda == NULL"
		 * because the Palm doesn't have a serial number).
		 */
		Error(_("Can't look up Palm."));

		palm_CSDisconnect(palm);
		return -1;
	}

	if (pda == NULL)
	{
		/* There's no PDA block defined in .coldsyncrc that matches
		 * this Palm. Hence, the username on the Palm should be the
		 * current user's name from /etc/passwd, and the userid
		 * should be the current user's UID.
		 */
		want_userid = userinfo.uid;
		want_username = userinfo.fullname;
	} else {
		/* Found a matching PDA block. If it has defined a username
		 * and/or userid, then the Palm should have those values.
		 * Otherwise, the Palm should have the defaults as above.
		 */
		if (pda->userid_given)
			want_userid = pda->userid;
		else
			want_userid = userinfo.uid;

		if (pda->username == NULL)
			want_username = userinfo.fullname;
		else
			want_username = pda->username;
	}

	time(&now);
	Verbose(1, _("Sync for %s at %s"),
		((pda == NULL || pda->name == NULL) ?
		 "unnamed PDA" : pda->name),
		ctime(&now));

	/* See if the userid matches. */
	p_userid = palm_userid(palm);
	if (p_userid == 0 && !palm_ok(palm))
	{
		Error(_("Can't get user ID from Palm."));

		palm_CSDisconnect(palm);
		return -1;
	}

	if (p_userid != want_userid)
	{
		Error(_("This Palm has user ID %ld (I was expecting "
			"%ld).\n"
			"Syncing would most likely destroy valuable data. "
			"Please update your\n"
			"configuration file and/or initialize this Palm "
			"(with 'coldsync -mI')\n"
			"before proceeding.\n"
			"\tYour configuration file should contain a PDA "
			"block that looks\n"
			"something like this:"),
		      p_userid, want_userid);
		pda = find_pda_block(palm, False);
				/* There might be a PDA block in the config
				 * file with the appropriate serial number,
				 * but the wrong username or userid. Find
				 * it and use it for suggesting a pda
				 * block.
				 * We don't check whether 'pda' is NULL,
				 * because that's not an error.
				 */
		print_pda_block(stderr, pda, palm);
				/* Don't bother checking cs_errno because
				 * we're about to abort anyway.
				 */

		palm_Disconnect(palm, DLPCMD_SYNCEND_CANCEL);
		return -1;
	}

	/* See if the username matches */
	p_username = palm_username(palm);

	if (p_username == NULL && !palm_ok(palm))
	{
		Error(_("Can't get user name from Palm."));

		palm_CSDisconnect(palm);
		return -1;
	}

	if (strncmp(p_username, want_username, DLPCMD_USERNAME_LEN)
	    != 0)
	{
		Error(_(
"This Palm has user name \"%.*s\" (I was expecting \"%.*s\").\n"
"Syncing would most likely destroy valuable data. Please update your\n"
"configuration file and/or initialize this Palm (with 'coldsync -mI')\n"
"before proceeding.\n"
"\tYour configuration file should contain a PDA block that looks\n"
"something like this:"),
		      DLPCMD_USERNAME_LEN, p_username,
		      DLPCMD_USERNAME_LEN, want_username);
		pda = find_pda_block(palm, False);
				/* There might be a PDA block in the config
				 * file with the appropriate serial number,
				 * but the wrong username or userid. Find
				 * it and use it for suggesting a pda
				 * block.
				 * We don't check whether 'pda' is NULL,
				 * because that's not an error.
				 */
		print_pda_block(stderr, pda, palm);
				/* Don't bother checking cs_errno because
				 * we're about to abort anyway.
				 */

		palm_Disconnect(palm, DLPCMD_SYNCEND_CANCEL);
		return -1;
	}

	return do_sync( pda, palm );
}

int
run_mode_Backup(int argc, char *argv[])
{
	int err;
	const char *backupdir = NULL;	/* Where to put backup */
	struct Palm *palm;

	/* Parse arguments:
	 *	dir		- Dump everything to <dir>
	 *	dir file...	- Dump <file...> to <dir>
	 */
	if (argc == 0)
	{
		Error(_("No backup directory specified."));
		return -1;
	}

	backupdir = argv[0];
	argc--;
	argv++;

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
			Error(_("\"%s\" exists, but is not a directory."),
			      backupdir);
		} else {
			Error(_("No such directory: \"%s\"."),
			      backupdir);
		}

		return -1;
	}

	if( (palm = palm_Connect()) == NULL )
		return -1;

	/* Do the backup */
	if (argc == 0)
	{
		/* No databases named on the command line. Back everything
		 * up.
		 */
		SYNC_TRACE(2)
			fprintf(stderr, "Backing everything up.\n");

		err = full_backup(palm_pconn(palm), palm, backupdir);
		if (err < 0)
		{
			switch (cs_errno)
			{
			    case CSE_CANCEL:
				Error(_("Cancelled by Palm."));
				goto done;	/* Still want to upload
						 * log */

			    case CSE_NOCONN:
				palm_Disconnect(palm, DLPCMD_SYNCEND_OTHER);
				return -1;
			    default:
				break;
			}
		}
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
				Warn(_("No such database: \"%s\"."),
				     argv[i]);
				va_add_to_log(palm_pconn(palm), "%s %s - %s\n",
					      _("Backup"),
					      argv[i],
					      _("No such database"));
				continue;
			}

			/* Back up the database */
			err = backup(palm_pconn(palm), db, backupdir);
			if (err < 0)
			{
				switch (cs_errno)
				{
				    case CSE_CANCEL:
					Error(_("Cancelled by Palm."));
					goto done;	/* Still want to
							 * upload log */

				    case CSE_NOCONN:
					palm_Disconnect(palm, DLPCMD_SYNCEND_OTHER);
					return -1;
				    default:
					Warn(_("Error backing up \"%s\"."),
					     db->name);
					break;
				}
			}
		}
	}

  done:

	palm_Disconnect(palm, DLPCMD_SYNCEND_NORMAL);

	return 0;
}

int
run_mode_Restore(int argc, char *argv[])
{
	int err;
	int i;
	struct Palm *palm;


	if( (palm = palm_Connect()) == NULL )
		return -1;

	/* Parse arguments: for each argument, if it's a file, upload that
	 * file. If it's a directory, upload all databases in that
	 * directory.
	 */
	for (i = 0; i < argc; i++)
	{
		if (is_directory(argv[i]))
		{
			/* Restore all databases in argv[i] */
			err = restore_dir(palm_pconn(palm), palm, argv[i]);
			if (err < 0)
			{
				switch (cs_errno)
				{
				    case CSE_CANCEL:
					Error(_("Cancelled by Palm."));
					goto done;
				    case CSE_NOCONN:
					palm_Disconnect(palm, DLPCMD_SYNCEND_OTHER);
					return -1;
				    default:
					Error(_("Can't restore "
						"directory."));
					break;
				}
				palm_Disconnect(palm, DLPCMD_SYNCEND_CANCEL);
				return -1;
			}
		} else {
			/* Restore the file argv[i] */
			err = restore_file(palm_pconn(palm), palm, argv[i]);
			if (err < 0)
			{
				switch (cs_errno)
				{
				    case CSE_CANCEL:
					Error(_("Cancelled by Palm."));
					goto done;
				    case CSE_NOCONN:
					palm_Disconnect(palm, DLPCMD_SYNCEND_OTHER);
					return -1;
				    default:
					Error(_("Can't restore "
						"directory."));
					break;
				}
				palm_Disconnect(palm, DLPCMD_SYNCEND_CANCEL);
				return -1;
			}
		}
	}

  done:

	palm_Disconnect(palm, DLPCMD_SYNCEND_NORMAL );

	return 0;
}

/* run_mode_Init
 * This mode merely initializes the Palm: it writes the appropriate user
 * information (name and userid), as well as NetSync information.
 * The point behind having this mode is that the username/userid identifies
 * the Palm and its user, both to ColdSync, and to HotSync. If ColdSync
 * overwrites this information, it will lead to problems when the user
 * syncs with HotSync.
 * In addition, a blank (factory-fresh, newly-reset, or never-synced) Palm
 * will have a userid of 0. In this case, doing a plain sync is the Wrong
 * Thing to do, as it will erase everything on the Palm (the Bargle bug).
 * Hence, Standalone mode doesn't modify the username/userid, but does
 * require it to match what's in the config file.
 */
int
run_mode_Init(int argc, char *argv[])
{
	int err;
	struct Palm *palm;
	Bool do_update;			/* Should we update the info on
					 * the Palm? */
	pda_block *pda;			/* PDA block from config file */
	const char *p_username;		/* Username on the Palm */
	const char *new_username;	/* What the username should be */
	udword p_userid;		/* Userid on the Palm */
	udword new_userid = 0;		/* What the userid should be */
	int p_snum_len;			/* Length of serial number on Palm */

	if( (palm = palm_Connect()) == NULL )
		return -1;

	/* Get the Palm's serial number, if possible */
	p_snum_len = palm_serial_len(palm);
	if (p_snum_len < 0)
	{
		Error(_("Can't read length of serial number."));

		palm_Disconnect(palm, DLPCMD_SYNCEND_CANCEL);
		return -1;
	}
	if (p_snum_len > 0)
	{
		const char *p_snum;	/* Serial number on Palm */
		char checksum;		/* Serial number checksum */

		p_snum = palm_serial(palm);
		if (p_snum == NULL)
		{
			Error(_("Can't read serial number."));

			palm_Disconnect(palm, DLPCMD_SYNCEND_CANCEL);
			return -1;
		}

		/* Calculate the checksum for the serial number */
		checksum = snum_checksum(p_snum, p_snum_len);
		SYNC_TRACE(2)
			fprintf(stderr, "Serial number is \"%s-%c\"\n",
				p_snum, checksum);
	}

	/* Get the PDA block for this Palm, from the config file(s) */
	pda = find_pda_block(palm, False);

	/* Decide what to update, if anything.
	 * The "do no harm" rule applies here. If the Palm already has
	 * non-default values for anything and the .coldsyncrc doesn't,
	 * then don't clobber what's already on the Palm: instead, tell the
	 * user what's there.
	 */
	do_update = True;		/* Assume we're going to update
					 * information on the Palm. */

	/* Username */
	p_username = palm_username(palm);
	if ((p_username == NULL) && !palm_ok(palm))
	{
		/* Something went wrong */
		Error(_("Can't get user name from Palm."));
		palm_CSDisconnect(palm);
		return -1;
	}

	SYNC_TRACE(4)
	{
		fprintf(stderr, "user name on Palm == [%s]\n",
			(p_username == NULL ? "NULL" : p_username));
		fprintf(stderr, "(Unix) userinfo.fullname == [%s]\n",
			userinfo.fullname);
	}

	if ((p_username == NULL) || (p_username[0] == '\0'))
	{
		/* The Palm doesn't specify a user name */
		if ((pda == NULL) || (pda->username == NULL))
		{
			/* .coldsyncrc doesn't specify a user name, either. */
			new_username = userinfo.fullname;
					/* This was set by load_config() */
		} else {
			/* The PDA block specifies a user name.
			 * NB: this name might be the empty string, but if
			 * the user wants to shoot himself in the foot this
			 * way, that's fine by me.
			 */
			new_username = pda->username;
		}
	} else {
		/* The Palm already specifies a user name */
		if ((pda == NULL) || (pda->username == NULL))
		{
			/* .coldsyncrc doesn't specify a user name */
			if (strncmp(p_username, userinfo.fullname,
				    DLPCMD_USERNAME_LEN) != 0)
			{
				/* The username on the Palm doesn't match
				 * what's in /etc/passwd.
				 * Don't clobber the information on the
				 * Palm; tell the user what's going on.
				 */
				do_update = False;
				new_username = p_username;
			} else {
				/* .coldsyncrc doesn't specify a user name,
				 * but what's on the Palm matches what's in
				 * /etc/passwd, so that's okay.
				 */
				new_username = userinfo.fullname;
			}
		} else {
			/* .coldsyncrc specifies a user name. This should
			 * be uploaded.
			 */
			new_username = pda->username;
		}
	}

	/* User ID */
	p_userid = palm_userid(palm);	/* Get userid from Palm */
	if ((p_userid == 0) && !palm_ok(palm))
	{
		/* Something went wrong */
		Error(_("Can't get user ID from Palm."));

		palm_CSDisconnect(palm);
		return -1;
	}

	SYNC_TRACE(4)
	{
		fprintf(stderr, "userid on Palm == %ld\n", p_userid);
		fprintf(stderr, "(Unix) userid == %ld\n",
			(long) userinfo.uid);
	}

	if (p_userid == 0)
	{
		/* The Palm doesn't specify a userid */
		if ((pda == NULL) || (!pda->userid_given))
		{
			/* .coldsyncrc doesn't specify a user ID either */
			new_userid = userinfo.uid;
					/* This was set by load_config() */
		} else {
			/* The PDA block specifies a user ID.
			 * NB: this might be 0, but if the user chooses to
			 * shoot himself in the foot this way, I'm not
			 * going to stop him.
			 */
			new_userid = pda->userid;
		}
	} else {
		/* The Palm already specifies a user ID */
		if ((pda == NULL) || (!pda->userid_given))
		{
			/* .coldsynrc doesn't specify a user ID */
			if (p_userid != userinfo.uid)
			{
				/* The userid on the Palm doesn't match
				 * this user's (Unix) uid.
				 * Don't clobber the information on the
				 * Palm; tell the user what's going on.
				 */
				do_update = False;
			} else {
				/* .coldsyncrc doesn't specify a user ID,
				 * but the one on the Palm matches the
				 * user's uid, so that's okay.
				 */
				new_userid = userinfo.uid;
			}
		} else {
			/* .coldsyncrc specifies a user ID. This should be
			 * updated.
			 */
			new_userid = pda->userid;
		}
	}

	if (!do_update)
	{
		/* Tell the user what the PDA block should look like */
		printf(_(
"\n"
"This Palm already has user information that matches neither what's in your\n"
"configuration file, nor the defaults. Please edit your .coldsyncrc and\n"
"reinitialize.\n"
"\n"
"Your .coldsyncrc should contain something like the following:\n"
"\n"));
		print_pda_block(stderr, pda, palm);
	} else {

		/* Update the user information on the Palm */

		struct dlp_setuserinfo uinfo;

		/* Clear uinfo */
		memset( &uinfo, sizeof(struct dlp_userinfo), 0x00);

		/* XXX - Set viewer ID? */

		/* Se the values we want to update */

		uinfo.userid		= new_userid;
		uinfo.lastsyncPC	= hostid;
		uinfo.username		= new_username;
				
		/* Time of last successful sync */
		{
			time_t now;				/* Current time */

			time(&now);				/* Get current time */
			time_time_t2dlp(now, &uinfo.lastsync);	/* Convert to DLP time */
		}

		/* XXX - Update last sync PC */
		/* XXX - Update last sync time */
		/* XXX - Update last successful sync time */

		err = UpdateUserInfo2(palm, &uinfo);
		if (err != (int) DLPSTAT_NOERR)
		{
			/* XXX - This message doesn't belog here. */
			Warn(_("DlpWriteUserInfo failed: %d."),
			     err);

			palm_Disconnect(palm, DLPCMD_SYNCEND_OTHER);
			return -1;
		}

		/* XXX - If the PDA block contains a "forward:" line with a
		 * non-"*" hostname, update the NetSync info on the Palm.
		 */
		if (pda->forward && (pda->forward_host != NULL))
		{
			/* XXX - Look up pda->forward_host. Or, if it's an
			 * address with a netmask, notice this and deal
			 * appropriately.
			 * If pda->forward_host is an address (with
			 * optional netmask), figure this out and convert
			 * both to strings.
			 * Otherwise, if it's a host address, get its
			 * address and convert it to a string
			 * (inet_ntop()).
			 * Otherwise, if it's a network name, figure out
			 * its netmask (how? Assume pre-CIDR class rules?
			 * What about IPv6?) and convert both to strings.
			 */
			/* XXX - If pda->forward_name is non-NULL, use
			 * that. Otherwise, use either the host or network
			 * name obtained above.
			 */
			/* XXX - Set 'lansync_on' to true: presumably if
			 * the user went to the trouble of adding a
			 * "forward:" line, it means he wants to use
			 * NetSync.
			 */
			/* XXX - Convert the above mess into a
			 * dlp_netsyncinfo structure and upload it to the
			 * Palm.
			 */
		}
	}

	/* Finally, close the connection */

	palm_Disconnect(palm, DLPCMD_SYNCEND_NORMAL);

	return 0;
}

int
run_mode_Daemon(int argc, char *argv[])
{
	int err;
	struct Palm *palm;
	pda_block *pda;			/* The PDA we're syncing with. */
	char *devname;			/* Name of device to open */
	char devbuf[MAXPATHLEN];	/* In case we need to construct
					 * device name */
	const struct palment *palment;	/* /etc/palms entry */
	struct passwd *pwent;		/* /etc/passwd entry */
	char *conf_fname = NULL;	/* Config file name from /etc/palms */

	SYNC_TRACE(3)
		fprintf(stderr, "Inside run_mode_Daemon()\n");

	/* If this mode ever takes additional getopt()-style arguments,
	 * parse them here.
	 */

	/* If port specified on command line, use that:
	 * "-": use stdin
	 * "/.*": use the given device
	 * "foo": try /dev/foo
	 */
	if (argc > 0)
	{
		fprintf(stderr,
			"OBSOLETE: Please use -p %s instead.\n",
			argv[0]);
	
		/* A port was specified on the command line. */
		SYNC_TRACE(3)
			fprintf(stderr,
				"Port specified on command line: [%s]\n",
				argv[0]);

		SYNC_TRACE(3)
			fprintf(stderr, "Listen type: %d\n", (int) global_opts.devtype);

		SYNC_TRACE(3)
			fprintf(stderr, "Protocol: %d\n", (int) global_opts.protocol);

		/* Figure out which device to use */
		if (strcmp(argv[0], "-") == 0)
		{
			/* "-": use stdin */
			SYNC_TRACE(3)
				fprintf(stderr,
					"Using stdin as Palm device\n");
			devname = NULL;
		} else if (argv[0][0] == '/')
		{
			/* Full pathname specified */
			SYNC_TRACE(3)
				fprintf(stderr, "Using [%s] as Palm device.\n",
					argv[0]);
			devname = argv[0];
		} else {
			/* Pathname relative to /dev */
			SYNC_TRACE(3)
				fprintf(stderr,
					"Using /dev/[%s] as Palm device.\n",
					argv[0]);
			snprintf(devbuf, sizeof(devbuf),
				 "/dev/%s", argv[0]);
			devname = devbuf;
		}
		
		if (prepend_listen_block(devname, global_opts.devtype, global_opts.protocol) < 0)
		{
			/* XXX - Maybe we should tell something to the user? */
			return -1;
		}

	} else {
		/* No port specified on command line. Get listen block from
		 * coldsyncrc.
		 */

		SYNC_TRACE(3)
			fprintf(stderr, "Using port from config file.\n");
	}

	/* Connect to the Palm */
	if( (palm = palm_Connect()) == NULL )
		return -1;

	/* Check if this palm is uninitialized and if autoinit is true*/
	if (palm_userid(palm) == 0 && global_opts.autoinit == True)
	{
		/* XXX If the palm hasn't a serial number autoinit will not work.
		 * We may want to upload some special program (like ChangeName)
		 * to allow the user to manually initialize his palm.
		 */
	
		/* Yes, get the best match */
		palment = lookup_palment(palm, PMATCH_SERIAL);

		if (palment != NULL)
		{
			struct dlp_setuserinfo newinfo;					
	
			newinfo.username = palment->username;
			newinfo.userid	 = palment->userid;
			
			err = UpdateUserInfo2(palm, &newinfo); 
			
			err = palm_reload(palm);		
		}
		else
		{
			Error(_("No matching entry in %s, couldn't autoinit."), _PATH_PALMS);
			/* XXX - Write reason to Palm log */
			palm_Disconnect(palm, DLPCMD_SYNCEND_CANCEL);
			return -1; 
		}
	}

	/* Look up this Palm in /etc/palms */
	/* XXX - Figure out exactly what the search criteria should be */
	/* XXX - If userid is 0, abort? */

	palment = lookup_palment(palm, PMATCH_EXACT);

	if (palment == NULL)
	{
		Error(_("No matching entry in %s."), _PATH_PALMS);
		/* XXX - Write reason to Palm log */

		palm_Disconnect(palm, DLPCMD_SYNCEND_CANCEL);
		return -1; 
	}

	/* Find the matching passwd structure */
	pwent = getpasswd_from_palment(palment);
	
	if( pwent == NULL)
	{
		Error(_("Unknown user/uid: \"%s\"."),
			palment->luser);
		/* XXX - Write reason to Palm log */

		palm_Disconnect(palm, DLPCMD_SYNCEND_CANCEL);
		return -1;
	}

	/* Setuid to the user whose Palm this is */
	err = setuid(pwent->pw_uid);
	if (err < 0)
	{
		Error(_("Can't setuid"));
		Perror("setuid");
		/* XXX - Write reason to Palm log */

		palm_Disconnect(palm, DLPCMD_SYNCEND_CANCEL);
		return -1;
	}

	/* If the /etc/palms entry specifies a config file, pretend that it
	 * was given with "-f" on the command line.
	 */
	if ((palment->conf_fname != NULL) &&
	    (palment->conf_fname[0] != '\0'))
	{
		/* Make a local copy of the config file name */
		conf_fname = strdup(palment->conf_fname);
		if (conf_fname == NULL)
		{
			Error(_("Can't copy config file name"));
			/* XXX - Write reason to Palm log */
			palm_Disconnect(palm, DLPCMD_SYNCEND_CANCEL);
			return -1;
		}
		global_opts.conf_fname = conf_fname;
		global_opts.conf_fname_given = True;
	}

	/* Now that we've setuid() to the proper user, clear the current
	 * configuration, and load it again, this time from the user's
	 * perspective.
	 */
	free_sync_config(sync_config); 
	if ((err = load_config(True)) < 0)
	{
		Error(_("Can't load configuration."));
		/* XXX - Write reason to Palm log */
		if (conf_fname != NULL)
			free(conf_fname);
		palm_Disconnect(palm, DLPCMD_SYNCEND_CANCEL);
		return -1;
	}

	/* We don't need conf_fname anymore */
	if (conf_fname != NULL)
	{
		free(conf_fname);
		conf_fname = NULL;
	}

	/* Look up the PDA in the user's configuration */
	pda = find_pda_block(palm, True);

	return do_sync( pda, palm );
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
		 * '1', which are too easily confused with the letters 'O'
		 * and 'I'.
		 */

	/* Convert to a character and return it */
	return (char) (checksum < 10 ?
		       checksum + '0' :
		       checksum - 10 + 'A');
}

/* This is for Emacs's benefit:
 * Local Variables:	***
 * fill-column:	75	***
 * End:			***
 */
