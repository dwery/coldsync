/* coldsync.c
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: coldsync.c,v 1.92 2001-03-30 06:29:50 arensb Exp $
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

#include <arpa/nameser.h>	/* Solaris's <resolv.h> requires this */
#include <resolv.h>		/* For inet_ntop() under Solaris */
#include <unistd.h>		/* For sleep(), getopt() */
#include <ctype.h>		/* For isalpha() and friends */
#include <errno.h>		/* For errno. Duh. */
#include <time.h>		/* For ctime() */
#include <syslog.h>		/* For syslog() */
#include <sys/types.h>		/* For getpwent() */
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

int sync_trace = 0;		/* Debugging level for sync-related stuff */
int misc_trace = 0;		/* Debugging level for miscellaneous stuff */

extern char *synclog;		/* Log that'll be uploaded to the Palm. See
				 * rant in "log.c".
				 */

static int Connect(PConnection *pconn);
static int Disconnect(PConnection *pconn, const ubyte status);
static int run_mode_Standalone(int argc, char *argv[]);
static int run_mode_Backup(int argc, char *argv[]);
static int run_mode_Restore(int argc, char *argv[]);
static int run_mode_Init(int argc, char *argv[]);
static int run_mode_Daemon(int argc, char *argv[]);
static int mkforw_addr(struct Palm *palm,
		       pda_block *pda,
		       struct sockaddr **addr,
		       socklen_t *sa_len);
int forward_netsync(PConnection *local, PConnection *remote);

int CheckLocalFiles(struct Palm *palm);
int UpdateUserInfo(PConnection *pconn,
		   const struct Palm *palm, const int success);
int reserve_fd(int fd, int flags);

int need_slow_sync;	/* XXX - This is bogus. Presumably, this should be
			 * another field in 'struct Palm' or 'sync_config'.
			 */

int cs_errno = CSE_NOERR;	/* ColdSync error code. */
struct cmd_opts global_opts;	/* Command-line options */
struct sync_config *sync_config = NULL;
				/* Configuration for the current sync */
struct pref_item *pref_cache = NULL;
				/* Preference cache */
FILE *oldstderr = NULL;		/* stderr, before it was redirected */

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
	global_opts.use_syslog		= False;
	global_opts.log_fname		= NULL;
	global_opts.do_backup		= False;
	global_opts.backupdir		= NULL;
	global_opts.do_restore		= False;
	global_opts.restoredir		= NULL;
	global_opts.force_install	= False;
	global_opts.install_first	= True;
	global_opts.verbosity		= 0;

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
	net_trace	= 0;

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
		Error(_("Can't reserve file descriptor %d."), 0);
		exit(1);
	}
	if (reserve_fd(1, O_WRONLY) < 0)
	{
		Error(_("Can't reserve file descriptor %d."), 1);
		exit(1);
	}
	if (reserve_fd(2, O_RDONLY) < 0)
	{
		Error(_("Can't reserve file descriptor %d."), 2);
		exit(1);
	}

	/* Get this host's hostid */
	if ((err = get_hostinfo()) < 0)
	{
		Error(_("Can't get host ID."));
		goto done;
	}

	/* Parse command-line arguments */
	err = parse_args(argc, argv);
	if (err < 0)
	{
		/* Error in command-line arguments */
		Error(_("Can't parse command-line arguments."));
		exit(1);
	}
	if (err == 0)
		/* Not an error, but no need to go on (e.g., the user just
		 * wanted the usage message.
		 */
		goto done;

	argc -= err;		/* Skip the parsed command-line options */
	argv += err;

	/* Open syslog, if requested */
	if (global_opts.use_syslog)
	{
		openlog(PACKAGE, LOG_PID, LOG_LOCAL0);
				/* XXX - Perhaps the facility should be
				 * configurable at configure-time.
				 */
	}

	/* Open the logfile, if requested.
	 * This block opens the log file given by the "-l" option, then
	 * redirects stderr to it. We also keep a copy of the original
	 * stderr as 'oldstderr', in case we ever need it.
	 * I think this is almost equivalent to
	 *	coldsync 3>&2 2>logfile
	 * in the Bourne shell.
	 * The main reason for doing this is daemon mode: when a process is
	 * run from getty or inetd, stderr is connected to the Palm, or the
	 * network socket. Printing error messages there would screw things
	 * up. The obvious thing to do would be to use "2>logfile", but
	 * getty and inetd don't give us a Bourne shell with which to do
	 * this.
	 */
	if (global_opts.log_fname != NULL)
	{
		int stderr_copy;	/* File descriptor of copy of stderr */
		int log_fd;		/* Temporary file descriptor for
					 * logfile */
		int log_fd2;		/* Temporary file descriptor for
					 * logfile */
		time_t now;		/* For timestamp */

		MISC_TRACE(1)
			fprintf(stderr,
				"Redirecting stderr to \"%s\"\n",
				global_opts.log_fname);

		/* Make a copy of the old stderr, in case we need it later. */
		stderr_copy = dup(STDERR_FILENO);
		if (stderr_copy < 0)
		{
			Error(_("Can't dup(stderr)."));
			Perror("dup");
			goto done;
		}
		oldstderr = fdopen(stderr_copy, "a");

		/* Open log file (with permissions that are either fascist
		 * or privacy-enhancing, depending on your point of view).
		 */
		log_fd = open(global_opts.log_fname, O_WRONLY|O_APPEND|O_CREAT,
			      0600);
		if (log_fd < 0)
		{
			Error(_("Can't open log file \"%s\"."),
			      global_opts.log_fname);
			Perror("open");
			goto done;
		}

		/* Move the log file from whichever file descriptor it's on
		 * now, to stderr's.
		 */
		log_fd2 = dup2(log_fd, STDERR_FILENO);
		if (log_fd2 < 0)
		{
			Error(_("Can't open log file on stderr."));
			Perror("dup2");
			close(log_fd);
			goto done;
		}

		/* Get rid of the now-unneeded duplicate log file descriptor */
		close(log_fd);

		/* Write a timestamp to the log file */
		now = time(NULL);
		fprintf(stderr, _("Log started on %s"), ctime(&now));
	}

	/* Load the configuration: in daemon mode, just load the global
	 * configuration from /etc/coldsync.conf. In other modes, read the
	 * current user's ~/.coldsyncrc as well.
	 */
	if (global_opts.mode == mode_Daemon)
		err = load_config(False);
	else
		err = load_config(True);
	if (err < 0)
	{
		Error(_("Can't load configuration."));
		goto done;
	}

	MISC_TRACE(1)
		/* So I'll know what people are running when they send me
		 * their log file.
		 */
		print_version(stderr);

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
		    case mode_Daemon:
			fprintf(stderr, "Daemon\n");
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
		fprintf(stderr, "\tuse_syslog: %s\n",
			global_opts.use_syslog ? "True" : "False");
		fprintf(stderr, "\tlog_fname: \"%s\"\n",
			global_opts.log_fname == NULL ?
				"(null)" : global_opts.log_fname);

		fprintf(stderr, "\nhostid == 0x%08lx (%02d.%02d.%02d.%02d)\n",
			hostid,
			(int) ((hostid >> 24) & 0xff),
			(int) ((hostid >> 16) & 0xff),
			(int) ((hostid >>  8) & 0xff),
			(int)  (hostid        & 0xff));
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
	    case mode_Init:
		err = run_mode_Init(argc, argv);
		break;
	    case mode_Daemon:
		err = run_mode_Daemon(argc, argv);
		break;
	    default:
		/* This should never happen */
		Error(_("Unknown mode: %d.\n"
			"This is a bug. Please report it to the "
			"maintainer."),
		      global_opts.mode);
		err = -1;
	}

  done:	/* Generic termination label. Here we take care of cleaning up
	 * after everything, and exit. If 'err' is negative, exit with an
	 * error status; otherwise, exit with a 0 status.
	 */
	if (sync_config != NULL)
	{
		free_sync_config(sync_config);
		sync_config = NULL;
	}

	if (pref_cache != NULL)
	{
		MISC_TRACE(6)
			fprintf(stderr, "Freeing pref_cache\n");
		FreePrefList(pref_cache);
	}

	if (synclog != NULL)
		free(synclog);

	if (hostaddrs != NULL)
		free_hostaddrs();

	MISC_TRACE(1)
		fprintf(stderr, "ColdSync terminating normally\n");

	/* Write a timestamp to the log file */
	if (global_opts.log_fname != NULL)
	{
		time_t now;

		now = time(NULL);
		fprintf(stderr, _("\nLog closed on %s"), ctime(&now));
	}

	if (global_opts.use_syslog)
		/* Close syslog */
		closelog();

	if (err < 0)
		exit(-err);
	exit(0);

	/*NOTREACHED*/
}

static int
run_mode_Standalone(int argc, char *argv[])
{
	int err;
	PConnection *pconn;		/* Connection to the Palm */
	struct pref_item *pref_cursor;
	const struct dlp_dbinfo *cur_db;
					/* Used when iterating over all
					 * databases */
	struct dlp_dbinfo dbinfo;	/* Used when installing files */
	struct Palm *palm;
	pda_block *pda;			/* The PDA we're syncing with. */
	const char *p_username;		/* The username on the Palm */
	const char *want_username;	/* The username we expect to see on
					 * the Palm. */
	udword p_userid;		/* The userid on the Palm */
	udword want_userid;		/* The userid we expect to see on
					 * the Palm. */
	udword p_lastsyncPC;		/* Hostid of last host Palm synced
					 * with */

	/* Get listen block */
	if (sync_config->listen == NULL)
	{
		Error(_("No port specified."));
		return -1;
	}

	SYNC_TRACE(2)
		fprintf(stderr, "Opening device [%s]\n",
			sync_config->listen->device);

	/* Set up a PConnection to the Palm */
	if ((pconn = new_PConnection(sync_config->listen->device,
				     sync_config->listen->listen_type, 1))
	    == NULL)
	{
		Error(_("Can't open connection."));
		/* XXX - Say why */
		return -1;
	}
	pconn->speed = sync_config->listen->speed;

	/* Connect to the Palm */
	if ((err = Connect(pconn)) < 0)
	{
		Error(_("Can't connect to Palm."));
		/* XXX - Say why */
		PConnClose(pconn);
		return -1;
	}

	/* Allocate a new Palm description */
	if ((palm = new_Palm(pconn)) == NULL)
	{
		Error(_("Can't allocate struct Palm."));
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
		return -1;
	}

	/* Figure out which Palm we're dealing with */
	pda = find_pda_block(palm, True);
	if (pda == NULL)
	{
		/* Error-checking (not to be confused with "pda == NULL"
		 * because the Palm doesn't have a serial number).
		 */
		switch (cs_errno)
		{
		    case CSE_NOERR:	/* No error */
			break;
		    case CSE_NOCONN:
			Error(_("Lost connection to Palm."));
			/* Don't even try to Disconnect(), since that tries
			 * to talk to the Palm.
			 */
			return -1;
		    default:
			Error(_("Can't look up Palm."));
			Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
			return -1;
		}
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

	/* See if the userid matches. */
	p_userid = palm_userid(palm);
	if (p_userid == 0)
	{
		switch (cs_errno)
		{
		    case CSE_NOERR:	/* No error */
			break;
		    case CSE_NOCONN:
			Error(_("Lost connection to Palm."));
			/* Don't even try to Disconnect(), since that tries
			 * to talk to the Palm.
			 */
			return -1;
		    default:
			Error(_("Can't get user ID from Palm."));
			Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
			return -1;
		}
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

		free_Palm(palm);
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
		return -1;
	}

	/* See if the username matches */
	p_username = palm_username(palm);
	if (p_username == NULL)
	{
		switch (cs_errno)
		{
		    case CSE_NOERR:	/* No error */
			break;
		    case CSE_NOCONN:
			Error(_("Lost connection to Palm."));
			/* Don't even try to Disconnect(), since that tries
			 * to talk to the Palm.
			 */
			return -1;
		    default:
			Error(_("Can't get user name from Palm."));
			Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
			return -1;
		}
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

		free_Palm(palm);
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
		return -1;
	}

	/* XXX - The rest of this function is the same as for daemon mode
	 * after setuid(), so it should be abstracted into a separate
	 * function.
	 */

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
		/* XXX - Need to figure out if remote address is really
		 * local, in which case we really need to continue doing a
		 * normal sync.
		 */
		struct sockaddr *sa;
		socklen_t sa_len;
		PConnection *pconn_forw;

		SYNC_TRACE(2)
			fprintf(stderr,
				"I ought to forward this sync to \"%s\" "
				"(%s)\n",
				(pda->forward_host == NULL ? "<whatever>" :
				 pda->forward_host),
				(pda->forward_name == NULL ? "(null)" :
				 pda->forward_name));

		/* Get list of addresses corresponding to this host. We do
		 * this now and not during initialization because in this
		 * age of dynamic DNS and whatnot, you can't assume that a
		 * machine will have the same addresses all the time.
		 */
		/* XXX - OTOH, most machines don't change addresses that
		 * quickly. So perhaps it'd be better to call
		 * get_hostaddrs() earlier, in case there are errors. Then,
		 * if the desired address isn't found, rerun
		 * get_hostaddrs() and see if that address has magically
		 * appeared.
		 */
		if ((err = get_hostaddrs()) < 0)
		{
			Error(_("Can't get host addresses."));
			free_Palm(palm);
			Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
			return -1;
		}

		err = mkforw_addr(palm, pda, &sa, &sa_len);
		if (err < 0)
		{
			Error(_("Can't resolve forwarding address."));
			free_Palm(palm);
			Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
			return -1;
		}

		SYNC_TRACE(3)
		{
			char namebuf[128];

			fprintf(stderr, "Forwarding to host [%s]\n",
				inet_ntop(sa->sa_family,
					  &(((struct sockaddr_in *) sa)->sin_addr),
					  namebuf, 128));
		}
					  

		/* XXX - Check whether *sa is local. If it is, just do a
		 * local sync, normally.
		 */

		/* XXX - Perhaps the rest of this block should be put in
		 * forward_netsync()?
		 */
		/* Create a new PConnection for talking to the remote host */
		if ((pconn_forw = new_PConnection(NULL, LISTEN_NET, 0))
		    == NULL)
		{
			Error(_("Can't create connection to forwarding "
				"host."));
			free(sa);
			free_Palm(palm);
			Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
			return -1;
		}

		/* Establish a connection to the remote host */
		err = (*pconn_forw->io_connect)(pconn_forw, sa, sa_len);
		if (err < 0)
		{
			Error(_("Can't establish connection with forwarding "
				"host."));
			free(sa);
			free_Palm(palm);
			Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
			PConnClose(pconn_forw);
			return -1;
		}

		err = forward_netsync(pconn, pconn_forw);
		if (err < 0)
		{
			Error(_("Network sync forwarding failed."));
			free(sa);
			free_Palm(palm);
			Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
			PConnClose(pconn_forw);
			return -1;
		}

		free(sa);
		free_Palm(palm);
		PConnClose(pconn);
		PConnClose(pconn_forw);
		return 0;
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
		free_Palm(palm);
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
		return -1;
	}

	/* Initialize (per-user) conduits */
	MISC_TRACE(1)
		fprintf(stderr, "Initializing conduits\n");

	/* Initialize preference cache */
	MISC_TRACE(1)
		fprintf(stderr,"Initializing preference cache\n");
	if ((err = CacheFromConduits(sync_config->conduits, pconn)) < 0)
	{
		Error(_("CacheFromConduits() returned %d."), err);
		free_Palm(palm);
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
		return -1;
	}

	/* Find out whether we need to do a slow sync or not */
	/* XXX - Actually, it's not as simple as this (see comment below) */
	p_lastsyncPC = palm_lastsyncPC(palm);
	if ((p_lastsyncPC == 0) && (cs_errno != CSE_NOERR))
	{
		Error(_("Can't get last sync PC from Palm"));
		free_Palm(palm);
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
		return -1;
	}

	if (hostid == p_lastsyncPC)
	{
		/* We synced with this same machine last time, so we can do
		 * a fast sync this time.
		 */
		Verbose(1, _("Doing a fast sync."));
		need_slow_sync = 0;
	} else {
		/* The Palm synced with some other machine, the last time
		 * it synced. We need to do a slow sync.
		 */
		Verbose(1, _("Doing a slow sync."));
		need_slow_sync = 1;
	}

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
		switch (cs_errno)
		{
		    case CSE_NOCONN:
			Error(_("Lost connection with Palm."));
			break;
		    default:
			Error(_("Can't fetch list of databases."));
			break;
		}
		free_Palm(palm);
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
		return -1;
	}

	MISC_TRACE(1)
		fprintf(stderr, "Doing a sync.\n");


	/* Install new databases before sync, if the config says so */
	if (global_opts.install_first)
	{
		/* Run any install conduits on the dbs in install directory
		 * Notice that install conduits are *not* run on files
		 * named for install on the command line.
		 */
		Verbose(1, _("Running Install conduits"));
		while (NextInstallFile(&dbinfo)>=0) {
			err = run_Install_conduits(&dbinfo);
			if (err < 0) {
				Error(_("Error %d running install "
					"conduits."),
				      err);
				free_Palm(palm);
				Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
				return -1;
			}
		}

		err = InstallNewFiles(pconn, palm, installdir, True);
		if (err < 0)
		{
			Error(_("Can't install new files."));

			free_Palm(palm);
			Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
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
	 * err = InstallNewFiles(pconn, &palm, "/tmp/palm-install",
	 * 		      False);
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
	Verbose(1, _("Running Fetch conduits"));
	palm_resetdb(palm);
	while ((cur_db = palm_nextdb(palm)) != NULL)
	{
		err = run_Fetch_conduits(cur_db);
		if (err < 0)
		{
			Error(_("Error %d running pre-fetch conduits."),
			      err);

			free_Palm(palm);
			Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
			return -1;
		}
	}

	/* See how the above loop terminated */
	if (cs_errno == CSE_NOCONN)
	{
		Error(_("Lost connection with Palm."));
		free_Palm(palm);
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
		return -1;
	}

	/* Synchronize the databases */
	Verbose(1, _("Running Sync conduits"));
	palm_resetdb(palm);
	while ((cur_db = palm_nextdb(palm)) != NULL)
	{
		/* Run the Sync conduits for this database. This includes
		 * built-in conduits.
		 */
		Verbose(2, "Syncing %s", cur_db->name);
		err = run_Sync_conduits(cur_db, pconn);
		if (err < 0)
		{
			switch (cs_errno)
			{
			    case CSE_CANCEL:
				Warn(_("Sync cancelled."));
				add_to_log(_("*Cancelled*\n"));
				DlpAddSyncLogEntry(pconn, synclog);
					/* Doesn't really matter if it
					 * fails, since we're terminating
					 * anyway.
					 */
				break;

			    case CSE_NOCONN:
				Error(_("Lost connection to Palm."));
				break;

			    default:
				Warn(_("Conduit failed for unknown "
				       "reason."));
				/* Continue, and hope for the best */
				continue;
			}

			free_Palm(palm);
			Disconnect(pconn, DLPCMD_SYNCEND_OTHER);

			return -1;
		}
	}

	/* See how the above loop terminated */
	switch (cs_errno)
	{
	    case CSE_CANCEL:
		Error(_("Sync cancelled by Palm."));
		free_Palm(palm);
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
		return -1;
	    case CSE_NOCONN:
		Error(_("Lost connection with Palm."));
		free_Palm(palm);
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
		return -1;
	    default:
		/* No error, nor not an important one */
		break;
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
			free_Palm(palm);
			Disconnect(pconn, DLPCMD_SYNCEND_OTHER);
			return -1;
		    default:
			/* Hope for the best */
			break;
		}
	}

	/* XXX - Write updated NetSync info */
	/* Write updated user info */
	if ((err = UpdateUserInfo(pconn, palm, 1)) < 0)
	{
		Error(_("Can't write user info."));
		free_Palm(palm);
		Disconnect(pconn, DLPCMD_SYNCEND_OTHER);

		return -1;
	}

	/* Upload sync log */
	if (synclog != NULL)
	{
		SYNC_TRACE(2)
			fprintf(stderr, "Writing log to Palm\n");

		if ((err = DlpAddSyncLogEntry(pconn, synclog)) < 0)
		{
			switch (palm_errno)
			{
			    case PALMERR_TIMEOUT:
				cs_errno = CSE_NOCONN;
				break;
			    default:
				break;
			}

			Error(_("Couldn't write sync log."));
			free_Palm(palm);
			Disconnect(pconn, DLPCMD_SYNCEND_OTHER);

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
		{
			err = FetchPrefItem(pconn, pref_cursor);
			if (err < 0)
			{
				switch (cs_errno)
				{
				    case CSE_NOCONN:
					Error(_("Lost connection to Palm."));
					free_Palm(palm);
					Disconnect(pconn,
						   DLPCMD_SYNCEND_OTHER);
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
		/* Run any install conduits on the dbs in install directory
		 * Notice that install conduits are *not* run on files
		 * named for install on the command line.
		 */
		Verbose(1, _("Running Install conduits"));
		while (NextInstallFile(&dbinfo)>=0) {
			err = run_Install_conduits(&dbinfo);
			if (err < 0) {
				Error(_("Error %d running install "
					"conduits."),
				      err);
				free_Palm(palm);
				Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
				return -1;
			}
		}

		err = InstallNewFiles(pconn, palm, installdir, True);
		if (err < 0)
		{
			Error(_("Can't install new files."));

			free_Palm(palm);
			Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
			return -1;
		}

	}

	/* Finally, close the connection */
	SYNC_TRACE(3)
		fprintf(stderr, "Closing connection to Palm\n");

	if ((err = Disconnect(pconn, DLPCMD_SYNCEND_NORMAL)) < 0)
	{
		Error(_("Couldn't disconnect."));
		return -1;
	}

	pconn = NULL;

	/* Run Dump conduits */
	Verbose(1, _("Running Dump conduits"));
	palm_resetdb(palm);
	while ((cur_db = palm_nextdb(palm)) != NULL)
	{
		err = run_Dump_conduits(cur_db);
		if (err < 0)
		{
			Error(_("Error %d running post-dump conduits."),
			      err);
			break;
		}
	}

	/* See how the above loop terminated */
	if (cs_errno == CSE_NOCONN)
	{
		Error(_("Lost connection with Palm."));
		free_Palm(palm);
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
		return -1;
	}

	free_Palm(palm);

	return 0;
}

static int
run_mode_Backup(int argc, char *argv[])
{
	int err;
	const char *backupdir = NULL;	/* Where to put backup */
	PConnection *pconn;		/* Connection to the Palm */
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
			Error(_("No backup directory specified."));
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
			Error(_("\"%s\" exists, but is not a directory."),
			      backupdir);
		} else {
			Error(_("No such directory: \"%s\"."),
			      backupdir);
		}

		return -1;
	}

	/* Get listen block */
	if (sync_config->listen == NULL)
	{
		Error(_("No port specified."));
		return -1;
	}

	SYNC_TRACE(2)
		fprintf(stderr, "Opening device [%s]\n",
			sync_config->listen->device);

	/* Set up a PConnection to the Palm */
	if ((pconn = new_PConnection(sync_config->listen->device,
				     sync_config->listen->listen_type, 1))
	    == NULL)
	{
		Error(_("Can't open connection."));
		return -1;
	}
	pconn->speed = sync_config->listen->speed;

	/* Connect to the Palm */
	if ((err = Connect(pconn)) < 0)
	{
		Error(_("Can't connect to Palm."));
		PConnClose(pconn);
		return -1;
	}

	/* Allocate a new Palm description */
	if ((palm = new_Palm(pconn)) == NULL)
	{
		Error(_("Can't allocate struct Palm."));
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
		if (err < 0)
		{
			switch (cs_errno)
			{
			    case CSE_CANCEL:
				Error(_("Cancelled by Palm."));
				goto done;	/* Still want to upload
						 * log */

			    case CSE_NOCONN:
				Error(_("Lost connection to Palm."));
				Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
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
				add_to_log(_("Backup "));
				add_to_log(argv[i]);
				add_to_log(" - ");
				add_to_log(_("No such database\n"));
				continue;
			}

			/* Back up the database */
			err = backup(pconn, db, backupdir);
			if (err < 0)
			{
				switch (cs_errno)
				{
				    case CSE_CANCEL:
					Error(_("Cancelled by Palm."));
					goto done;	/* Still want to
							 * upload log */

				    case CSE_NOCONN:
					Error(_("Lost connection to Palm."));
					Disconnect(pconn,
						   DLPCMD_SYNCEND_CANCEL);
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
	/* Upload sync log */
	if (synclog != NULL)
	{
		SYNC_TRACE(2)
			fprintf(stderr, "Writing log to Palm\n");

		if ((err = DlpAddSyncLogEntry(pconn, synclog)) < 0)
		{
			switch (palm_errno)
			{
			    case PALMERR_TIMEOUT:
				cs_errno = CSE_NOCONN;
				break;
			    default:
				break;
			}

			Error(_("Couldn't write sync log."));
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
		Error(_("Couldn't disconnect."));
		return -1;
	}
	pconn = NULL;

	return 0;
}

static int
run_mode_Restore(int argc, char *argv[])
{
	int err;
	int i;
	PConnection *pconn;		/* Connection to the Palm */
	struct Palm *palm;

	/* Get listen block */
	if (sync_config->listen == NULL)
	{
		Error(_("No port specified."));
		return -1;
	}

	SYNC_TRACE(2)
		fprintf(stderr, "Opening device [%s]\n",
			sync_config->listen->device);

	/* Set up a PConnection to the Palm */
	if ((pconn = new_PConnection(sync_config->listen->device,
				     sync_config->listen->listen_type, 1))
	    == NULL)
	{
		Error(_("Can't open connection."));
		return -1;
	}
	pconn->speed = sync_config->listen->speed;

	/* Connect to the Palm */
	if ((err = Connect(pconn)) < 0)
	{
		Error(_("Can't connect to Palm."));
		PConnClose(pconn);
		return -1;
	}

	/* Allocate a new Palm description */
	if ((palm = new_Palm(pconn)) == NULL)
	{
		Error(_("Can't allocate struct Palm."));
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
		return -1;
	}

	/* Parse arguments: for each argument, if it's a file, upload that
	 * file. If it's a directory, upload all databases in that
	 * directory.
	 */
	if (global_opts.do_restore)
	{
		/* Compatibility mode: the user has specified "-r <dir>".
		 * Restore everything in <dir>.
		 */
		err = restore_dir(pconn, palm, global_opts.backupdir);
		if (err < 0)
		{
			switch (cs_errno)
			{
			    case CSE_CANCEL:
				Error(_("Cancelled by Palm."));
				goto done;
			    case CSE_NOCONN:
				Error(_("Lost connection to Palm."));
				break;
			    default:
				Error(_("Can't restore directory."));
			}
			Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
			return -1;
		}
	} else {
		for (i = 0; i < argc; i++)
		{
			if (is_directory(argv[i]))
			{
				/* Restore all databases in argv[i] */

				err = restore_dir(pconn, palm, argv[i]);
				if (err < 0)
				{
					switch (cs_errno)
					{
					    case CSE_CANCEL:
						Error(_("Cancelled by Palm."));
						goto done;
					    case CSE_NOCONN:
						Error(_("Lost connection to "
							"Palm."));
						break;
					    default:
						Error(_("Can't restore "
							"directory."));
						break;
					}
					Disconnect(pconn,
						   DLPCMD_SYNCEND_CANCEL);
					return -1;
				}
			} else {
				/* Restore the file argv[i] */

				err = restore_file(pconn, palm, argv[i]);
				if (err < 0)
				{
					switch (cs_errno)
					{
					    case CSE_CANCEL:
						Error(_("Cancelled by Palm."));
						goto done;
					    case CSE_NOCONN:
						Error(_("Lost connection to "
							"Palm."));
						break;
					    default:
						Error(_("Can't restore "
							"directory."));
						break;
					}
					Disconnect(pconn,
						   DLPCMD_SYNCEND_CANCEL);
					return -1;
				}
			}
		}
	}

  done:
	/* Upload sync log */
	if (synclog != NULL)
	{
		SYNC_TRACE(2)
			fprintf(stderr, "Writing log to Palm\n");

		if ((err = DlpAddSyncLogEntry(pconn, synclog)) < 0)
		{
			switch (palm_errno)
			{
			    case PALMERR_TIMEOUT:
				cs_errno = CSE_NOCONN;
				break;
			    default:
				break;
			}

			Error(_("Couldn't write sync log."));
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
		Error(_("Couldn't disconnect."));
		return -1;
	}
	pconn = NULL;

	free_Palm(palm);

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
static int
run_mode_Init(int argc, char *argv[])
{
	int err;
	PConnection *pconn;		/* Connection to the Palm */
	struct Palm *palm;
	Bool do_update;			/* Should we update the info on
					 * the Palm? */
	pda_block *pda;			/* PDA block from config file */
	const char *p_username;		/* Username on the Palm */
	const char *new_username;	/* What the username should be */
	udword p_userid;		/* Userid on the Palm */
	udword new_userid = 0;		/* What the userid should be */
	int p_snum_len;			/* Length of serial number on Palm */

	/* Get listen block */
	if (sync_config->listen == NULL)
	{
		Error(_("No port specified."));
		return -1;
	}

	SYNC_TRACE(2)
		fprintf(stderr, "Opening device [%s]\n",
			sync_config->listen->device);

	/* Set up a PConnection to the Palm */
	if ((pconn = new_PConnection(sync_config->listen->device,
				     sync_config->listen->listen_type, 1))
	    == NULL)
	{
		Error(_("Can't open connection."));
		return -1;
	}
	pconn->speed = sync_config->listen->speed;

	/* Connect to the Palm */
	if ((err = Connect(pconn)) < 0)
	{
		Error(_("Can't connect to Palm."));
		PConnClose(pconn);
		return -1;
	}

	/* Allocate a new Palm description */
	if ((palm = new_Palm(pconn)) == NULL)
	{
		Error(_("Can't allocate struct Palm."));
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
		return -1;
	}

	/* Get the Palm's serial number, if possible */
	p_snum_len = palm_serial_len(palm);
	if (p_snum_len < 0)
	{
		Error(_("Can't read length of serial number."));
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
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
			Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
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
	if ((p_username == NULL) && (cs_errno != CSE_NOERR))
	{
		/* Something went wrong */
		free_Palm(palm);
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
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
				new_username = pda->username;
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
	if ((p_userid == 0) && (cs_errno != CSE_NOERR))
	{
		/* Something went wrong */
		Error(_("Can't get user ID from Palm."));
		free_Palm(palm);
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
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
		/* XXX - This section mostly duplicates UpdateUserInfo().
		 * Is this a bad idea? Is UpdateUserInfo() broken?
		 */
		struct dlp_setuserinfo uinfo;
		char uidbuf[16];	/* Buffer for printed representation
					 * of userid */

		SYNC_TRACE(1)
			fprintf(stderr, "Updating user info.\n");

		uinfo.modflags = 0; 

		/* User ID */
		/* XXX - Should see if it's necessary to update this */
		SYNC_TRACE(4)
			fprintf(stderr, "Setting the userid to %ld\n",
				new_userid);

		uinfo.userid = new_userid;
		uinfo.modflags |= DLPCMD_MODUIFLAG_USERID;

		add_to_log(_("Set user ID: "));
		sprintf(uidbuf, "%lu\n", uinfo.userid);
		add_to_log(uidbuf);

		/* XXX - Set viewer ID? */

		/* Last sync PC */
		SYNC_TRACE(3)
			fprintf(stderr, "Setting lastsyncPC to 0x%08lx\n",
				hostid);
		uinfo.lastsyncPC = hostid;
		uinfo.modflags |= DLPCMD_MODUIFLAG_SYNCPC;

		/* Time of last successful sync */
		{
			time_t now;		/* Current time */

			SYNC_TRACE(3)
				fprintf(stderr,
					"Setting last sync time to now\n");
			time(&now);		/* Get current time */
			time_time_t2dlp(now, &uinfo.lastsync);
						/* Convert to DLP time */
			uinfo.modflags |= DLPCMD_MODUIFLAG_SYNCDATE;
		}

		/* User name */
		/* XXX - Should see if it's necessary to update this */
		SYNC_TRACE(4)
			fprintf(stderr, "Setting the username to [%s]\n",
				(new_username == NULL ? "NULL" :
				 new_username));
		uinfo.usernamelen = strlen(new_username)+1;
		uinfo.username = new_username;
		uinfo.modflags |= DLPCMD_MODUIFLAG_USERNAME;

		add_to_log(_("Set username: "));
		add_to_log(uinfo.username);
		add_to_log("\n");

		/* XXX - Update last sync PC */
		/* XXX - Update last sync time */
		/* XXX - Update last successful sync time */

		err = DlpWriteUserInfo(pconn, &uinfo);
		if (err != DLPSTAT_NOERR)
		{
			switch (palm_errno)
			{
			    case PALMERR_TIMEOUT:
				cs_errno = CSE_NOCONN;
				break;
			    default:
				break;
			}

			Warn(_("DlpWriteUserInfo failed: %d."),
			     err);
			free_Palm(palm);
			Disconnect(pconn, DLPCMD_SYNCEND_OTHER);
			pconn = NULL;
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

	/* Upload sync log */
	if (synclog != NULL)
	{
		SYNC_TRACE(2)
			fprintf(stderr, "Writing log to Palm\n");

		if ((err = DlpAddSyncLogEntry(pconn, synclog)) < 0)
		{
			switch (palm_errno)
			{
			    case PALMERR_TIMEOUT:
				cs_errno = CSE_NOCONN;
				break;
			    default:
				break;
			}

			Error(_("Couldn't write sync log."));
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
		Error(_("Couldn't disconnect."));
		return -1;
	}

	pconn = NULL;

	free_Palm(palm);

	return 0;
}

static int
run_mode_Daemon(int argc, char *argv[])
{
	int err;
	PConnection *pconn;		/* Connection to the Palm */
	struct pref_item *pref_cursor;
	const struct dlp_dbinfo *cur_db;
					/* Used when iterating over all
					 * databases */
	struct dlp_dbinfo dbinfo;	/* Used when installing files */
	struct Palm *palm;
	pda_block *pda;			/* The PDA we're syncing with. */
	char *devname;			/* Name of device to open */
	char devbuf[MAXPATHLEN];	/* In case we need to construct
					 * device name */
	run_mode devtype;		/* Listen block type */
	const struct palment *palment;	/* /etc/palms entry */
	struct passwd *pwent;		/* /etc/passwd entry */
	char *conf_fname = NULL;	/* Config file name from /etc/palms */
	const char *p_username;		/* Username on Palm */
	const char *p_snum;		/* Serial number on Palm */
	udword p_userid;		/* User ID on Palm */
	udword p_lastsyncPC;		/* Hostid of last host Palm synced
					 * with */

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
		/* A port was specified on the command line. */
		SYNC_TRACE(3)
			fprintf(stderr,
				"Port specified on command line: [%s]\n",
				argv[0]);

		/* Use the listen type specified by the '-t' option */
		devtype = global_opts.devtype; 

		SYNC_TRACE(3)
			fprintf(stderr, "Listen type: %d\n", devtype);

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
	} else {
		/* No port specified on command line. Get listen block from
		 * coldsyncrc.
		 */
		SYNC_TRACE(3)
			fprintf(stderr, "Using port from config file.\n");

		if (sync_config->listen == NULL)
		{
			Error(_("No port specified."));
			return -1;
		}
		devname = sync_config->listen->device;
		devtype = sync_config->listen->listen_type;

		SYNC_TRACE(3)
		{
			fprintf(stderr, "Device: [%s]\n", devname);
			fprintf(stderr, "Listen type: %d\n", devtype);
		}
	}

	/* Set up a PConnection to the Palm */
	if ((pconn = new_PConnection(devname, devtype, False)) == NULL)
	{
		Error(_("Can't open connection."));
		/* XXX - Say why */
		return -1;
	}
	pconn->speed = sync_config->listen->speed;

	/* Connect to the Palm */
	if ((err = Connect(pconn)) < 0)
	{
		Error(_("Can't connect to Palm."));
		/* XXX - Say why */
		PConnClose(pconn);
		return -1;
	}

	/* Allocate a new Palm description */
	if ((palm = new_Palm(pconn)) == NULL)
	{
		Error(_("Can't allocate struct Palm."));
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
		return -1;
	}

	/* Look up this Palm in /etc/palms */
	/* XXX - Figure out exactly what the search criteria should be */
	/* XXX - Should allow '*' in fields as wildcard */
	/* XXX - If userid is 0, abort? */

	/* Get username */
	p_username = palm_username(palm);
	if ((p_username == NULL) && (cs_errno != CSE_NOERR))
	{
		/* Something went wrong */
		free_Palm(palm);
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
		return -1;
	}

	/* Get userid */
	p_userid = palm_userid(palm);
	if ((p_userid == 0) && (cs_errno != CSE_NOERR))
	{
		/* Something went wrong */
		free_Palm(palm);
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
		return -1;
	}

	/* Get serial number */
	p_snum = palm_serial(palm);
	if ((p_snum == NULL) && (cs_errno != CSE_NOERR))
	{
		/* Something went wrong */
		free_Palm(palm);
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
		return -1;
	}

	SYNC_TRACE(3)
	{
		fprintf(stderr, "Looking for PDA in [" _PATH_PALMS "]\n");
		fprintf(stderr,
			"Want serial [%s], username [%s], userid %lu\n",
			p_snum, p_username,
			p_userid);
	}

	while ((palment = getpalment()) != NULL)
	{
		char entserial[SNUM_MAX];	/* Serial number from
						 * /etc/palms, but without
						 * the checksum.
						 */
		char *dashp;			/* Pointer to "-" */

		/* Get the serial number, but without the checksum */
		/* XXX - Actually, this should look for the pattern
		 *	/-[A-Z]$/
		 * since in the future, there might be a special serial
		 * number like "*Visor-Plus*" which would match.
		 */
		if (palment->serial != NULL)
		{
			strncpy(entserial, p_snum, SNUM_MAX);
			dashp = strrchr(entserial, '-');
			if (dashp != NULL)
				*dashp = '\0';
		} else {
			entserial[0] = '\0';
		}

		SYNC_TRACE(3)
			fprintf(stderr,
				"Found serial [%s], username [%s], "
				"userid %lu\n",
				palment->serial, palment->username,
				palment->userid);

		if (strncasecmp(entserial, p_snum, SNUM_MAX) != 0)
		{
			SYNC_TRACE(4)
				fprintf(stderr,
					"Serial number [%s]/[%s] "
					"doesn't match.\n",
					palment->serial, entserial);
			continue;
		}
		SYNC_TRACE(5)
			fprintf(stderr, "Serial number [%s]/[%s] matches.\n",
				palment->serial, entserial);

		if ((palment->username != NULL) &&
		    (palment->username[0] != '\0') &&
		    strncmp(palment->username, p_username,
			    DLPCMD_USERNAME_LEN) != 0)
		{
			SYNC_TRACE(4)
				fprintf(stderr,
					"Username [%s] doesn't match\n",
					palment->username);
			continue;
		}
		SYNC_TRACE(5)
			fprintf(stderr, "Username [%s] matches\n",
				palment->username);

		if (palment->userid != p_userid)
		{
			SYNC_TRACE(4)
				fprintf(stderr,
					"Userid %lu doesn't match %lu\n",
					palment->userid,
					p_userid);
			continue;
		}
		SYNC_TRACE(5)
			fprintf(stderr, "Userid %lu matches\n",
				palment->userid);

		break;	/* If we get this far, this entry matches */
	}
	endpalment();

	if (palment == NULL)
	{
		Error(_("No matching entry in %s."), _PATH_PALMS);
		/* XXX - Write reason to Palm log */
		free_Palm(palm);
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
		return -1; 
	}

	SYNC_TRACE(3)
		fprintf(stderr,
			"Found a match. luser [%s], name [%s], "
			"conf_fname [%s]\n",
			palment->luser, palment->name, palment->conf_fname);

	pwent = getpwnam(palment->luser);
	if (pwent == NULL)
	{
		SYNC_TRACE(3)
			fprintf(stderr, "User name [%s] not found.\n",
				palment->luser);

		/* See if it's a numeric uid */
		if (strspn(palment->luser, "0123456789")
		    == strlen(palment->luser))
		{
			pwent = getpwuid(strtoul(palment->luser, NULL, 10));
			if (pwent == NULL)
			{
				SYNC_TRACE(3)
					fprintf(stderr,
						"numeric uid [%s] not found\n",
						palment->luser);
				Error(_("Unknown user/uid: \"%s\""),
				      palment->luser);
				/* XXX - Write reason to Palm log */
				free_Palm(palm);
				Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
				return -1;
			}
		}
	}

	SYNC_TRACE(3)
	{
		fprintf(stderr, "pw_name: [%s]\n", pwent->pw_name);
		fprintf(stderr, "pw_uid: [%ld]\n", (long) pwent->pw_uid);
	}

	/* Setuid to the user whose Palm this is */
	err = setuid(pwent->pw_uid);
	if (err < 0)
	{
		Error(_("Can't setuid"));
		Perror("setuid");
		/* XXX - Write reason to Palm log */
		free_Palm(palm);
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
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
			free_Palm(palm);
			Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
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
		Error(_("Can't load configuration"));
		/* XXX - Write reason to Palm log */
		if (conf_fname != NULL)
			free(conf_fname);
		free_Palm(palm);
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
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
		/* XXX - Need to figure out if remote address is really
		 * local, in which case we really need to continue doing a
		 * normal sync.
		 */
		struct sockaddr *sa;
		socklen_t sa_len;
		PConnection *pconn_forw;

		SYNC_TRACE(2)
			fprintf(stderr,
				"I ought to forward this sync to \"%s\" "
				"(%s)\n",
				(pda->forward_host == NULL ? "<whatever>" :
				 pda->forward_host),
				(pda->forward_name == NULL ? "(null)" :
				 pda->forward_name));

		/* Get list of addresses corresponding to this host. We do
		 * this now and not during initialization because in this
		 * age of dynamic DNS and whatnot, you can't assume that a
		 * machine will have the same addresses all the time.
		 */
		/* XXX - OTOH, most machines don't change addresses that
		 * quickly. So perhaps it'd be better to call
		 * get_hostaddrs() earlier, in case there are errors. Then,
		 * if the desired address isn't found, rerun
		 * get_hostaddrs() and see if that address has magically
		 * appeared.
		 */
		if ((err = get_hostaddrs()) < 0)
		{
			Error(_("Can't get host addresses."));
			free_Palm(palm);
			Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
			return -1;
		}

		err = mkforw_addr(palm, pda, &sa, &sa_len);
		if (err < 0)
		{
			Error(_("Can't resolve forwarding address."));
			free_Palm(palm);
			Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
			return -1;
		}

		SYNC_TRACE(3)
		{
			char namebuf[128];

			fprintf(stderr, "Forwarding to host [%s]\n",
				inet_ntop(sa->sa_family,
					  &(((struct sockaddr_in *) sa)->sin_addr),
					  namebuf, 128));
		}

		/* XXX - Check whether *sa is local. If it is, just do a
		 * local sync, normally.
		 */

		/* XXX - Perhaps the rest of this block should be put in
		 * forward_netsync()?
		 */
		/* Create a new PConnection for talking to the remote host */
		if ((pconn_forw = new_PConnection(NULL, LISTEN_NET, 0))
		    == NULL)
		{
			Error(_("Can't create connection to forwarding "
				"host."));
			free(sa);
			free_Palm(palm);
			Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
			return -1;
		}

		/* Establish a connection to the remote host */
		err = (*pconn_forw->io_connect)(pconn_forw, sa, sa_len);
		if (err < 0)
		{
			Error(_("Can't establish connection with forwarding "
				"host."));
			free(sa);
			free_Palm(palm);
			Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
			PConnClose(pconn_forw);
			return -1;
		}

		err = forward_netsync(pconn, pconn_forw);
		if (err < 0)
		{
			Error(_("Network sync forwarding failed."));
			free(sa);
			free_Palm(palm);
			Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
			PConnClose(pconn_forw);
			return -1;
		}

		free(sa);
		free_Palm(palm);
		PConnClose(pconn);
		PConnClose(pconn_forw);
		return 0;
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
		free_Palm(palm);
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
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
		Error(_("CacheFromConduits() returned %d."), err);
		free_Palm(palm);
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
		return -1;
	}

	/* Find out whether we need to do a slow sync or not */
	/* XXX - Actually, it's not as simple as this (see comment below) */
	p_lastsyncPC = palm_lastsyncPC(palm);
	if ((p_lastsyncPC == 0) && (cs_errno != CSE_NOERR))
	{
		Error(_("Can't get last sync PC from Palm"));
		free_Palm(palm);
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
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
		switch (cs_errno)
		{
		    case CSE_NOCONN:
			Error(_("Lost connection with Palm."));
			break;
		    default:
			Error(_("Can't fetch list of databases."));
			break;
		}
		free_Palm(palm);
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
		return -1;
	}

	MISC_TRACE(1)
		fprintf(stderr, "Doing a sync.\n");


	/* Install new databases before sync, if the config says so */
	if (global_opts.install_first)
	{
		/* Run any install conduits on the dbs in install directory
		 * Notice that install conduits are *not* run on files
		 * named for install on the command line.
		 */
		Verbose(1, _("Running Install conduits"));
		while (NextInstallFile(&dbinfo)>=0) {
			err = run_Install_conduits(&dbinfo);
			if (err < 0) {
				Error(_("Error %d running install "
					"conduits."),
				      err);
				free_Palm(palm);
				Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
				return -1;
			}
		}

		err = InstallNewFiles(pconn, palm, installdir, True);
		if (err < 0)
		{
			Error(_("Can't install new files."));

			free_Palm(palm);
			Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
			return -1;
		}
	}

	/* XXX - It should be possible to specify a list of directories to
	 * look in: that way, the user can put new databases in
	 * ~/.palm/install, whereas in a larger site, the sysadmin can
	 * install databases in /usr/local/stuff; they'll be uploaded from
	 * there, but not deleted.
	 * E.g.:
	 * err = InstallNewFiles(pconn, &palm, "/tmp/palm-install",
	 *		      False);
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
	palm_resetdb(palm);
	while ((cur_db = palm_nextdb(palm)) != NULL)
	{
		err = run_Fetch_conduits(cur_db);
		if (err < 0)
		{
			Error(_("Error %d running pre-fetch conduits."),
			      err);

			free_Palm(palm);
			Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
			return -1;
		}
	}

	/* See how the above loop terminated */
	if (cs_errno == CSE_NOCONN)
	{
		Error(_("Lost connection with Palm."));
		free_Palm(palm);
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
		return -1;
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
				Warn(_("Sync cancelled."));
				add_to_log(_("*Cancelled*\n"));
				DlpAddSyncLogEntry(pconn, synclog);
					/* Doesn't really matter if it
					 * fails, since we're terminating
					 * anyway.
					 */
				break;

			    case CSE_NOCONN:
				Error(_("Lost connection to Palm."));
				break;

			    default:
				Warn(_("Conduit failed for unknown "
				       "reason."));
				/* Continue, and hope for the best */
				continue;
			}

			free_Palm(palm);
			Disconnect(pconn, DLPCMD_SYNCEND_OTHER);

			return -1;
		}
	}

	/* See how the above loop terminated */
	if (cs_errno == CSE_NOCONN)
	{
		Error(_("Lost connection with Palm."));
		free_Palm(palm);
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
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
			free_Palm(palm);
			Disconnect(pconn, DLPCMD_SYNCEND_OTHER);
			return -1;
		    default:
			/* Hope for the best */
			break;
		}
	}

	/* XXX - Write updated NetSync info */
	/* Write updated user info */
	if ((err = UpdateUserInfo(pconn, palm, 1)) < 0)
	{
		Error(_("Can't write user info."));
		free_Palm(palm);
		Disconnect(pconn, DLPCMD_SYNCEND_OTHER);

		return -1;
	}

	/* Upload sync log */
	if (synclog != NULL)
	{
		SYNC_TRACE(2)
			fprintf(stderr, "Writing log to Palm\n");

		if ((err = DlpAddSyncLogEntry(pconn, synclog)) < 0)
		{
			switch (palm_errno)
			{
			    case PALMERR_TIMEOUT:
				cs_errno = CSE_NOCONN;
				break;
			    default:
				break;
			}

			Error(_("Couldn't write sync log."));
			free_Palm(palm);
			Disconnect(pconn, DLPCMD_SYNCEND_OTHER);

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
		{
			err = FetchPrefItem(pconn, pref_cursor);
			if (err < 0)
			{
				switch (cs_errno)
				{
				    case CSE_NOCONN:
					Error(_("Lost connection to Palm."));
					free_Palm(palm);
					Disconnect(pconn,
						   DLPCMD_SYNCEND_OTHER);
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
		/* Run any install conduits on the dbs in install directory
		 * Notice that install conduits are *not* run on files
		 * named for install on the command line.
		 */
		Verbose(1, _("Running Install conduits"));
		while (NextInstallFile(&dbinfo)>=0) {
			err = run_Install_conduits(&dbinfo);
			if (err < 0) {
				Error(_("Error %d running install "
					"conduits."),
				      err);
				free_Palm(palm);
				Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
				return -1;
			}
		}

		err = InstallNewFiles(pconn, palm, installdir, True);
		if (err < 0)
		{
			Error(_("Can't install new files."));

			free_Palm(palm);
			Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
			return -1;
		}

	}

	/* Finally, close the connection */
	SYNC_TRACE(3)
		fprintf(stderr, "Closing connection to Palm\n");

	if ((err = Disconnect(pconn, DLPCMD_SYNCEND_NORMAL)) < 0)
	{
		Error(_("Couldn't disconnect."));
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
			Error(_("Error %d running post-dump conduits."),
			      err);
			break;
		}
	}

	/* See how the above loop terminated */
	if (cs_errno == CSE_NOCONN)
	{
		Error(_("Lost connection with Palm."));
		free_Palm(palm);
		Disconnect(pconn, DLPCMD_SYNCEND_CANCEL);
		return -1;
	}

	free_Palm(palm);

	return 0;
}

/* Connect
 * Wait for a Palm to show up on the other end.
 */
static int
Connect(PConnection *pconn)
{
	struct slp_addr pcaddr;

	pcaddr.protocol = SLP_PKTTYPE_PAD;	/* XXX - This ought to be
						 * part of the initial
						 * socket setup.
						 */
	pcaddr.port = SLP_PORT_DLP;
	PConn_bind(pconn, &pcaddr, sizeof(struct slp_addr));
	if ((*pconn->io_accept)(pconn) < 0)
	{
		switch (palm_errno)
		{
		    case PALMERR_TIMEOUT:
			cs_errno = CSE_NOCONN;
			break;
		    default:
			break;
		}
		return -1;
	}

	return 0;
}

static int
Disconnect(PConnection *pconn, const ubyte status)
{
	int err;

	/* Terminate the sync */
	err = DlpEndOfSync(pconn, status);
	if (err < 0)
	{
		switch (palm_errno)
		{
		    case PALMERR_TIMEOUT:
			cs_errno = CSE_NOCONN;
			break;
		    default:
			break;
		}

		Error(_("Error during DlpEndOfSync: (%d) %s."),
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

/* UpdateUserInfo
 * Update the Palm's user info. 'success' indicates whether the sync was
 * successful.
 */
int
UpdateUserInfo(PConnection *pconn,
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
		switch (palm_errno)
		{
		    case PALMERR_TIMEOUT:
			cs_errno = CSE_NOCONN;
			break;
		    default:
			break;
		}

		Error(_("DlpWriteUserInfo failed: %d."), err);
		return -1;
	}

	return 0;		/* Success */
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

/* mkforw_addr
 * The pda block has forwarding turned on. Look at all the pertinent
 * information and figure out the address to which the connection needs to
 * be forwarded.
 * '*addr' and '*netmask' are filled in by mkforw_addr(). Upon successful
 * completion, they will be pointers to sockaddrs that can be passed to
 * various socket functions establish a connection with the remote host.
 *
 * Returns 0 if successful, or a negative value in case of error.
 */
static int
mkforw_addr(struct Palm *palm,
	    pda_block *pda,
	    struct sockaddr **sa,
	    socklen_t *sa_len)
{
	int err;
	const char *hostname;		/* Name (or address, as a string)
					 * of the host to which to forward
					 * the sync.
					 */
	struct hostent *hostent = NULL;	/* From gethostby*() */
	struct servent *service;	/* NetSync wakeup service entry */
	int wakeup_port;		/* NetSync wakeup port number */

	service = getservbyname("netsync-wakeup", "udp");
				/* Try to get the entry for
				 * "netsync-wakeup" from /etc/services */
	if (service == NULL)
		wakeup_port = htons(NETSYNC_WAKEUP_PORT);
	else
		wakeup_port = service->s_port;

	SYNC_TRACE(4)
	{
		if (service != NULL)
		{
			int i;

			fprintf(stderr, "Got entry for netsync-wakeup/udp:\n");
			fprintf(stderr, "\tname: \"%s\"\n", service->s_name);
			fprintf(stderr, "\taliases:\n");
			for (i = 0; service->s_aliases[i] != NULL; i++)
				fprintf(stderr, "\t\t\"%s\"\n",
					service->s_aliases[i]);
			fprintf(stderr, "\tport: %d\n",
				ntohs(service->s_port));
			fprintf(stderr, "\tprotocol: \"%s\"\n",
				service->s_proto);
		} else {
			fprintf(stderr, "No entry for netsync-wakeup/udp\n");
		}
	}

	/* Get the name of the host to forward the connection to.
	 * Try the name given on the "forward:" line in .coldsyncrc first.
	 * If there isn't one, ask the Palm for the address of its
	 * preferred server host. If that isn't set, try the hostname.
	 *
	 * XXX - Would it be better first to check whether the address
	 * resolves and, if that fails, try to use the hostname?
	 */
	hostname = pda->forward_host;
	if (hostname == NULL)
	{
		hostname = palm_netsync_hostaddr(palm);
		if ((hostname == NULL) && (cs_errno != CSE_NOERR))
			/* Something went wrong */
			return -1;
	}

	if (hostname == NULL)
	{
		hostname = palm_netsync_hostname(palm);
		if ((hostname == NULL) && (cs_errno != CSE_NOERR))
			/* Something went wrong */
			return -1;
	}

	SYNC_TRACE(3)
		fprintf(stderr, "forward hostname is [%s]\n",
			(hostname == NULL ? "(null)" : hostname));

#if HAVE_SOCKADDR6
	/* Try to look up the name as IPv6 hostname */
	if ((hostent = gethostbyname2(hostname, AF_INET6)) != NULL)
	{
		struct sockaddr_in6 *sa6;	/* Temporary */

		SYNC_TRACE(3)
			fprintf(stderr, "It's an IPv6 hostname\n");

		/* Allocate a new sockaddr */
		sa6 = (struct sockaddr_in6 *)
			malloc(sizeof(struct sockaddr_in6));
		if (sa6 == NULL)
			return -1;

		/* Fill in the new sockaddr */
		bzero((void *) sa6, sizeof(struct sockaddr_in6));
		sa6->sin6_family = AF_INET6;
		sa6->sin6_port = wakeup_port;
		memcpy(&sa6->sin6_addr,
		       hostent->h_addr_list[0],
		       sizeof(struct in6_addr));

		SYNC_TRACE(3)
		{
			char namebuf[128];

			debug_dump(stderr, "sa6",
				   (ubyte *) sa6,
				   sizeof(struct sockaddr_in));
			fprintf(stderr, "Returning address [%s]\n",
				inet_ntop(AF_INET6,
					  &(sa6->sin6_addr),
					  namebuf, 128));
		}

		*sa = (struct sockaddr *) sa6;
		*sa_len = sizeof(struct sockaddr_in6);
		return 0;		/* Success */
	}

	SYNC_TRACE(3)
		fprintf(stderr, "It's not an IPv6 hostname\n");
#endif	/* HAVE_SOCKADDR6 */

	/* Try to look up the name as IPv4 hostname */
	if ((hostent = gethostbyname2(hostname, AF_INET)) != NULL)
	{
		struct sockaddr_in *sa4;	/* Temporary */

		SYNC_TRACE(3)
			fprintf(stderr, "It's an IPv4 hostname\n");

		/* Allocate a new sockaddr */
		sa4 = (struct sockaddr_in *)
			malloc(sizeof(struct sockaddr_in));
		if (sa4 == NULL)
			return -1;

		/* Fill in the new sockaddr */
		bzero((void *) sa4, sizeof(struct sockaddr_in));
		sa4->sin_family = AF_INET;
		sa4->sin_port = wakeup_port;
		memcpy(&sa4->sin_addr,
		       hostent->h_addr_list[0],
		       sizeof(struct in_addr));

		SYNC_TRACE(3)
		{
			char namebuf[128];

			debug_dump(stderr, "sa4",
				   (ubyte *) sa4,
				   sizeof(struct sockaddr_in));
			fprintf(stderr, "Returning address [%s]\n",
				inet_ntop(AF_INET,
					  &(sa4->sin_addr),
					  namebuf, 128));
		}

		*sa = (struct sockaddr *) sa4;
		*sa_len = sizeof(struct sockaddr_in);
		return 0;		/* Success */
	}

	SYNC_TRACE(3)
		fprintf(stderr, "It's not an IPv4 hostname\n");

#if HAVE_SOCKADDR6
	/* Try to use the name as an IPv6 address */
	{
		struct in6_addr addr6;
		struct sockaddr_in6 *sa6;

		/* Try to convert the string to an IPv6 address */
		err = inet_pton(AF_INET6, hostname, &addr6);
		if (err < 0)
		{
			SYNC_TRACE(3)
				fprintf(stderr,
					"Error in inet_pton(AF_INET6)\n");

			Perror("inet_pton");
		} else if (err == 0)
		{
			SYNC_TRACE(3)
				fprintf(stderr,
					"It's not a valid IPv6 address\n");
		} else {
			/* Succeeded in converting to IPv6 address */
			SYNC_TRACE(3)
				fprintf(stderr, "It's an IPv6 address\n");

			/* Allocate a new sockaddr */
			sa6 = (struct sockaddr_in6 *)
				malloc(sizeof(struct sockaddr_in6));
			if (sa6 == NULL)
				return -1;

			/* Fill in the new sockaddr */
			bzero((void *) sa6, sizeof(struct sockaddr_in6));
			sa6->sin6_family = AF_INET6;
			sa6->sin6_port = wakeup_port;
			memcpy(&sa6->sin6_addr, &addr6,
			       sizeof(struct in6_addr));

			*sa = (struct sockaddr *) sa6;
			*sa_len = sizeof(struct sockaddr_in6);
			return 0;		/* Success */
		}
	}
#endif	/* HAVE_SOCKADDR6 */

	/* Try to use the name as an IPv4 address */
	{
		struct in_addr addr4;
		struct sockaddr_in *sa4;

		/* Try to convert the string to an IPv4 address */
		err = inet_pton(AF_INET, hostname, &addr4);
		if (err < 0)
		{
			SYNC_TRACE(3)
				fprintf(stderr,
					"Error in inet_pton(AF_INET)\n");

			Perror("inet_pton");
		} else if (err == 0)
		{
			SYNC_TRACE(3)
				fprintf(stderr,
					"It's not a valid IPv4 address\n");
		} else {
			/* Succeeded in converting to IPv4 address */
			SYNC_TRACE(3)
				fprintf(stderr, "It's an IPv4 address\n");

			/* Allocate a new sockaddr */
			sa4 = (struct sockaddr_in *)
				malloc(sizeof(struct sockaddr_in));
			if (sa4 == NULL)
				return -1;

			/* Fill in the new sockaddr */
			bzero((void *) sa4, sizeof(struct sockaddr_in));
			sa4->sin_family = AF_INET;
			sa4->sin_port = wakeup_port;
			memcpy(&sa4->sin_addr, &addr4,
			       sizeof(struct in_addr));

			*sa = (struct sockaddr *) sa4;
			*sa_len = sizeof(struct sockaddr_in);
			return 0;		/* Success */
		}
	}

	/* Nothing worked */
	SYNC_TRACE(3)
		fprintf(stderr, "Nothing worked. I give up.\n");

	return -1;
}

/* forward_netsync
 * Listen for packets from either pconn, and forward them to the other.
 */
int
forward_netsync(PConnection *local, PConnection *remote)
{
	int err;
	int maxfd;
	fd_set in_fds;
	fd_set out_fds;
	const ubyte *inbuf;
	uword inlen;

	/* Get highest-numbered file descriptor, for select() */
	maxfd = local->fd;
	if (remote->fd > maxfd)
		maxfd = remote->fd;

	for (;;)
	{
		FD_ZERO(&in_fds);
		FD_SET(local->fd, &in_fds);
		FD_SET(remote->fd, &in_fds);
		FD_ZERO(&out_fds);
		FD_SET(local->fd, &out_fds);
		FD_SET(remote->fd, &out_fds);

		err = select(maxfd+1, &in_fds, /*&out_fds*/NULL, NULL, NULL);
		SYNC_TRACE(5)
			fprintf(stderr, "select() returned %d\n", err);

		if (FD_ISSET(local->fd, &in_fds))
		{
			err = (*local->dlp.read)(local, &inbuf, &inlen);
			if (err < 0)
			{
				Perror("read local");
				break;
			}
			SYNC_TRACE(5)
				fprintf(stderr,
					"Read %d-byte message from local. "
					"err == %d\n",
					inlen, err);

			err = (*remote->dlp.write)(remote, inbuf, inlen);
			if (err < 0)
			{
				Perror("read local");

				switch (palm_errno)
				{
				    case PALMERR_TIMEOUT:
					cs_errno = CSE_NOCONN;
					break;
				    default:
					break;
				}

				break;
			}
			SYNC_TRACE(5)
				fprintf(stderr,
					"Wrote %d-byte message to remote. "
					"err == %d\n",
					inlen, err);
		}

		if (FD_ISSET(remote->fd, &in_fds))
		{
			err = (*remote->dlp.read)(remote, &inbuf, &inlen);
			if (err < 0)
			{
				Perror("read local");
				break;
			}
			SYNC_TRACE(5)
				fprintf(stderr,
					"Read %d-byte message from remote. "
					"err == %d\n",
					inlen, err);

			err = (*local->dlp.write)(local, inbuf, inlen);
			if (err < 0)
			{
				Perror("read local");

				switch (palm_errno)
				{
				    case PALMERR_TIMEOUT:
					cs_errno = CSE_NOCONN;
					break;
				    default:
					break;
				}

				break;
			}
			SYNC_TRACE(5)
				fprintf(stderr,
					"Wrote %d-byte message to local. "
					"err == %d\n",
					inlen, err);
		}
	}

	return 0;
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
		Error(_("%s: Can't create staging file \"%s\"."),
		      "open_tempfile", name_template);
		Perror("mkstemp");
		return -1;
	}

#else	/* HAVE_MKSTEMP */

	if (mktemp(name_template) == 0)
	{
		Error(_("%s: Can't create staging file name."),
		      "open_tempfile");
		return -1;
	}

	/* Open the output file */
	if ((retval = open(name_template,
			   O_WRONLY | O_CREAT | O_EXCL | O_BINARY,
			   0600)) < 0)
	{
		Error(_("%s: Can't create staging file \"%s\"."),
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
			Perror("reserve_fd: open");
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
			Perror("reserve_fd: dup2");
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
