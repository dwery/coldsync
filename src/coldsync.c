/* coldsync.c
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: coldsync.c,v 1.146 2002-11-13 20:14:38 azummo Exp $
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
#include "runmode.h"
#include "sync.h"

/* XXX - Add an inet_ntop() prototype for brain-damaged OSes that have it,
 * but don't have a prototype.
 */

int sync_trace = 0;		/* Debugging level for sync-related stuff */
int misc_trace = 0;		/* Debugging level for miscellaneous stuff */
int conduit_trace = 0;		/* Debugging level for conduits-related stuff */

static int open_log_file(void);


int reserve_fd(int fd, int flags);

int need_slow_sync;	/* XXX - This is bogus. Presumably, this should be
			 * another field in 'struct Palm' or 'sync_config'.
			 */

CSErrno cs_errno = CSE_NOERR;	/* ColdSync error code. */
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
	global_opts.devtype		= LISTEN_NONE;
	global_opts.protocol		= PCONN_STACK_DEFAULT;
	global_opts.use_syslog		= False;
	global_opts.log_fname		= NULL;
	global_opts.force_install	= Undefined;	/* Defaults to False */
	global_opts.install_first	= Undefined;	/* Defaults to True */
	global_opts.verbosity		= 0;
	global_opts.listen_name		= NULL;
	global_opts.autoinit		= Undefined;	/* Default to False */

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

#if USE_CAPABILITIES

	/* Drop all capabilities */
	err = cap_set_proc(cap_from_text("= cap_setuid+ep"));
	
	if (err < 0)
	{
		Error(_("Failed to set process capabilities/privileges."));
		exit(1);		
	}

#endif /* USE_CAPABILITIES */

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

	/* Initialize the global symbol table. */
	symboltable_init();

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

	/* Open the log file here (though we might reopen it later): for
	 * one thing, there might be errors from parsing the config
	 * file(s), and we want those to go to the Right Place.
	 *
	 * For another thing, command line overrides config file. If the
	 * log file is set only in the config file, then parsing errors go
	 * to stderr, and once the config file has been parsed, further
	 * errors go to the file specified in the config file.
	 *
	 * If the config file is set both on the command line and in the
	 * config file, then all errors go to the one specified on the
	 * command line.
	 */
	if (open_log_file() < 0)
		goto done;

	/* Load the configuration: in daemon mode, just load the global
	 * configuration from /etc/coldsync.conf. In other modes, read the
	 * current user's ~/.coldsyncrc as well.  On the other hand, if the
	 * user has specified the config file name on the command line, they
	 * really want it - even in Daemon mode..
	 */
	if (global_opts.mode == mode_Daemon)
		err = load_config(global_opts.conf_fname_given);
	else
		err = load_config(True);
	if (err < 0)
	{
		Error(_("Can't load configuration."));
		goto done;
	}

	/* Get this host's hostid, unless it was set by the config file */
	if ((hostid == 0L) && (err = get_hostinfo()) < 0)
	{
		Error(_("Can't get host ID."));
		goto done;
	}

	/* Log file is (re)opened after config file is read, since it can
	 * be set there.
	 */
	if (open_log_file() < 0)
		goto done;

	MISC_TRACE(1)
		/* So I'll know what people are running when they send me
		 * their log file.
		 */
		print_version(stderr);

	MISC_TRACE(2)
	{
		char *tmp;

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
			(int) global_opts.devtype);
		fprintf(stderr, "\tprotocol: %d\n",
			(int) global_opts.protocol);
		fprintf(stderr, "\tforce_slow: %s\n",
			global_opts.force_slow ? "True" : "False");
		fprintf(stderr, "\tforce_fast: %s\n",
			global_opts.force_fast ? "True" : "False");
		fprintf(stderr, "\tcheck_ROM: %s\n",
			global_opts.check_ROM ? "True" : "False");
		fprintf(stderr, "\tinstall_first: %s\n",
			Bool3str(global_opts.install_first));
		fprintf(stderr, "\tforce_install: %s\n",
			Bool3str(global_opts.force_install));
		fprintf(stderr, "\tautoinit: %s\n",
			Bool3str(global_opts.autoinit));
		fprintf(stderr, "\tuse_syslog: %s\n",
			global_opts.use_syslog ? "True" : "False");
		tmp = get_symbol("CS_LOGFILE");
		fprintf(stderr, "\tlog_fname: \"%s\"\n",
			(tmp == NULL ? "(null)" : tmp));

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
		fprintf(stderr, "\tSLP:     %d\n", slp_trace);
		fprintf(stderr, "\tCMP:     %d\n", cmp_trace);
		fprintf(stderr, "\tPADP:    %d\n", padp_trace);
		fprintf(stderr, "\tDLP:     %d\n", dlp_trace);
		fprintf(stderr, "\tDLPC:    %d\n", dlpc_trace);
		fprintf(stderr, "\tPDB:     %d\n", pdb_trace);
		fprintf(stderr, "\tSYNC:    %d\n", sync_trace);
		fprintf(stderr, "\tPARSE:   %d\n", parse_trace);
		fprintf(stderr, "\tIO:      %d\n", io_trace);
		fprintf(stderr, "\tMISC:    %d\n", misc_trace);
		fprintf(stderr, "\tCONDUIT: %d\n", conduit_trace);
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

	/* Print cs_errno if we got any error */	 
	if (cs_errno_fatal(cs_errno))	 
		print_cs_errno(cs_errno);			
	 
	if (sync_config != NULL)
	{
		free_sync_config(sync_config);
		sync_config = NULL;
	}

	symboltable_tini();	/* Clean up parser symbol table */

	if (pref_cache != NULL)
	{
		MISC_TRACE(6)
			fprintf(stderr, "Freeing pref_cache\n");
		FreePrefList(pref_cache);
	}

	if (hostaddrs != NULL)
		free_hostaddrs();

	MISC_TRACE(1)
		fprintf(stderr, "ColdSync terminating normally\n");

	/* Write a timestamp to the log file.
	 * The condition may be non-obvious: the idea is that we don't log
	 * this timestamp to stderr, only to a file specified with the "-l
	 * logfile" option, or $CS_LOGFILE variable. If we opened a separate
	 * log file, then 'oldstderr' was set to the old stderr.
	 */
	if (oldstderr != NULL)
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

/* open_log_file
 * Open a log file to which error messages will be sent. This is pretty
 * much equivalent to
 *	exec 3>&2 2>logfile
 * in the Bourne shell.
 *
 * If a log file has been specified on the command line, use that.
 * Otherwise, if the symbol "CS_LOGFILE" is defined, use that. Otherwise, just
 * have error messages go to stderr.
 *
 * Returns 0 if successful, or -1 in case of error.
 */
static int
open_log_file(void)
{
	const char *filename;	/* Log file pathname */
	int stderr_copy;	/* File descriptor of copy of stderr */
	int log_fd;		/* Temporary file descriptor for logfile */
	int log_fd2;		/* Temporary file descriptor for logfile */
	time_t now;		/* For timestamp */

	/* Figure out where to log: try command line, then config file,
	 * then environment variable (implied).
	 */
	if (global_opts.log_fname != NULL)
		filename = global_opts.log_fname;
	else {
		filename = get_symbol("CS_LOGFILE");
		if ((filename == NULL) || (filename[0] == '\0'))
			/* No log file, or empty log file specified. Don't
			 * do anything. */
			return 0;
	}

	/* Open the logfile, if requested.
	 * This next bit opens the log file given by 'filename', then
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
/*
	XXX With debugging enabled, this message actually goes
	 out on the socket!
	 
	MISC_TRACE(1)
		fprintf(stderr,
			"Redirecting stderr to \"%s\"\n",
			filename);
*/
	/* Make a copy of the old stderr, in case we need it later. But
	 * only if we haven't done so yet.
	 */
	if (oldstderr == NULL)
	{
		stderr_copy = dup(STDERR_FILENO);
		if (stderr_copy < 0)
		{
			Error(_("Can't dup(stderr)."));
			Perror("dup");
			return -1;
		}
		oldstderr = fdopen(stderr_copy, "a");
	}

	/* Open log file (with permissions that are either fascist
	 * or privacy-enhancing, depending on your point of view).
	 */
	log_fd = open(filename, O_WRONLY|O_APPEND|O_CREAT,
		      0600);
	if (log_fd < 0)
	{
		Error(_("Can't open log file \"%s\"."),
		      filename);
		Perror("open");
		return -1;
	}

	/* Move the log file from whichever file descriptor it's on now, to
	 * stderr's. If file descriptor 2 was already open, dup2() closes
	 * it first. This means that if open_log_file() is called twice, it
	 * does the Right Thing.
	 */
	log_fd2 = dup2(log_fd, STDERR_FILENO);
	if (log_fd2 < 0)
	{
		Error(_("Can't open log file on stderr."));
		Perror("dup2");
		close(log_fd);
		return -1;
	}

	/* Get rid of the now-unneeded duplicate log file descriptor */
	close(log_fd);

	/* Write a timestamp to the log file */
	now = time(NULL);
	fprintf(stderr, _("Log started on %s"), ctime(&now));
	return 0;
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
