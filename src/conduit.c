/* conduit.c
 *
 * Functions for dealing with conduits: looking them up, loading
 * libraries, invoking them, etc.
 *
 *	Copyright (C) 1999, 2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: conduit.c,v 2.2 2000-07-03 07:57:23 arensb Exp $
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <sys/types.h>			/* For pid_t, for select() */
#include <sys/time.h>			/* For select() */
#include <sys/wait.h>			/* For waitpid() */
#include <unistd.h>			/* For select() */
#include <signal.h>			/* For signal() */
#include <setjmp.h>			/* For sigsetjmp()/siglongjmp() */
#include <errno.h>			/* For errno. Duh */
#include <ctype.h>			/* For isdigit() and friends */

#if HAVE_STRINGS_H
#  include <strings.h>			/* For bzero() under AIX */
#endif	/* HAVE_STRINGS_H */

#if  HAVE_SYS_SELECT_H
#  include <sys/select.h>		/* To make select() work rationally
					 * under AIX */
#endif	/* HAVE_SYS_SELECT_H */

#if HAVE_LIBINTL_H
#  include <libintl.h>			/* For i18n */
#endif	/* HAVE_LIBINTL_H */

/* Bleah. AIX doesn't have WCOREDUMP */
#ifndef WCOREDUMP
# define WCOREDUMP(status)	0
#endif	/* WCOREDUMP */

#include "conduit.h"

typedef RETSIGTYPE (*sighandler) (int);	/* This is equivalent to FreeBSD's
					 * 'sig_t', but that's a BSDism.
					 */

static int run_conduits(struct dlp_dbinfo *dbinfo,
			char *flavor,
			unsigned short flavor_mask);
static pid_t spawn_conduit(const char *path,
			   char * const argv[],
			   FILE **tochild,
			   FILE **fromchild,
			   const fd_set *openfds);
static int cond_readline(char *buf,
			 int len,
			 FILE *fromchild);
static int cond_readstatus(FILE *fromchild);
static RETSIGTYPE sigchld_handler(int sig);

/* The following variables describe the state of the conduit process. They
 * are global variables because this data is set and used in different
 * functions.
 * Currently, ColdSync only spawns one child at a time, so there is only
 * one of each. When and if it becomes possible to spawn multiple children
 * at a time, this will need to be updated.
 */
static volatile pid_t conduit_pid = -1;	/* The PID of the currently-running
					 * conduit.
					 */
static int conduit_status = 0;		/* Last known status of the
					 * conduit, as set by waitpid().
					 */
static sigjmp_buf chld_jmpbuf;		/* Saved state for sigsetjmp() */
static volatile sig_atomic_t canjump = 0;
					/* Essentially a Bool: true iff the
					 * SIGCHLD handler can safely call
					 * longjmp() [*]
					 */
	/* [*] See Stevens, Richard W., "Advanced Programming in the UNIX
	 * Environment", Addison-Wesley, 1993, section 10.15, "sigsetjmp
	 * and siglongjmp Functions".
	 */

/* The following two variables are for setvbuf's benefit, for when we make
 * the conduit's stdin and stdout be line-buffered.
 */
#if 0	/* This isn't actually used anywhere */
static char cond_stdin_buf[BUFSIZ];	/* Buffer for conduit's stdin */
#endif	/* 0 */
static char cond_stdout_buf[BUFSIZ];	/* Buffer for conduit's stdout */

/* run_conduit
 * Run a single conduit, of the given flavor.
 * Returns a negative value in case of an error running the conduit.
 * Otherwise, returns the last status returned by the conduit (which may
 * indicate an error with the conduit).
 */
static int
run_conduit(struct dlp_dbinfo *dbinfo,
	    char *flavor,
	    conduit_block *conduit)
{
	int err;
	int i;
	static char * argv[4];	/* Conduit's argv */
	pid_t pid;		/* Conduit's PID */
	FILE *fromchild = NULL;	/* File handle to child's stdout */
	FILE *tochild = NULL;	/* File handle to child's stdin */
	const char *bakfname;	/* Path to backup file */
	volatile int laststatus = 501;
				/* The last status code printed by the
				 * child. This is used as its exit status.
				 * Defaults to 501, since that's a sane
				 * return status if the conduit doesn't run
				 * at all (e.g., it isn't executable).
				 */
	struct cond_header *hdr;	/* User-supplied header */
	sighandler old_sigchld;		/* Previous SIGCHLD handler */
	struct {		/* Array of standard headers */
		char *name;
		const char *value;
	} headers[] = {
		{ "Daemon",	PACKAGE },
		{ "Version",	VERSION },
		{ "InputDB",	NULL },
		{ "OutputDB",	NULL },
	};
	int num_headers = sizeof(headers) / sizeof(headers[0]);

	/* XXX - See if conduit->path is a predefined string (set up a
	 * table of built-in conduits). If so, run the corresponding
	 * built-in conduit.
	 */

	if (conduit->path == NULL)
		/* This conduit has no path. It does nothing. This can be
		 * useful for defining a do-nothing default.
		 */
		return 201;		/* Success (trivially) */

	/* Set handler for SIGCHLD, so that we can keep track of what
	 * happens to conduit child processes.
	 */
	old_sigchld = signal(SIGCHLD, sigchld_handler);
	if (old_sigchld == SIG_ERR)
	{
		fprintf(stderr, _("%s: Can't set signal handler\n"),
			"run_conduits");
		perror("signal");
		return -1;
	}

	/* When the child exits, sigchld_handler() will longjmp() back to
	 * here. This way, none of the other code has to worry about
	 * whether the conduit is still running.
	 */
	if ((err = sigsetjmp(chld_jmpbuf, 1)) != 0)
	{
		SYNC_TRACE(4)
			fprintf(stderr, "Returned from sigsetjmp(): %d\n",
				err);
		canjump = 0;
		goto abort;
	}

	canjump = 1;			/* Tell the SIGCHLD signal handler
					 * that it can call siglongjmp().
					 */

	argv[0] = conduit->path;	/* Path to conduit */
	argv[1] = "conduit";		/* Mandatory argument */
	argv[2] = flavor;		/* Flavor argument */
	argv[3] = NULL;			/* Terminator */

	pid = spawn_conduit(conduit->path,
			    argv,
			    &tochild, &fromchild,
			    NULL);
	if (pid < 0)
	{
		fprintf(stderr, "%s: Can't spawn conduit\n",
			"run_conduits");

		/* Let's hope that this isn't a fatal problem */
		goto abort;
	}

	/* Feed the various parameters to the child via 'tochild'. */

	/* Initialize the values that weren't initialized above */
	bakfname = mkbakfname(dbinfo);
	headers[2].value = bakfname;
	headers[3].value = bakfname;

	for (i = 0; i < num_headers; i++)
	{
		SYNC_TRACE(4)
			fprintf(stderr, ">>> %s: %s\n",
				headers[i].name,
				headers[i].value);
		fprintf(tochild, "%s: %s\n",
			headers[i].name,
			headers[i].value);
	}

	/* User-supplied header lines */
	for (hdr = conduit->headers; hdr != NULL; hdr = hdr->next)
	{
		SYNC_TRACE(4)
			fprintf(stderr, ">>> %s: %s\n",
				hdr->name,
				hdr->value);
		fprintf(tochild, "%s: %s\n",
			hdr->name,
			hdr->value);
	}

	/* Send an empty line to the child (end of input) */
	fprintf(tochild, "\n");
	fflush(tochild);

	/* Read from the child's stdout.
	 */
	/* XXX - This should really select() 'fromchild' and the SPC file
	 * descriptor (for Sync conduits). This time around, the select()
	 * should block on input.
	 */
	while ((err = cond_readstatus(fromchild)) > 0)
	{
		SYNC_TRACE(2)
			fprintf(stderr,
				"run_conduits: got status %d\n",
				err);

		/* Save the error status for later use: the last error
		 * status printed is the final result from the conduit.
		 */
		laststatus = err;
	}

  abort:
	/* The conduit has exited */

	/* See if there is anything pending on 'fromchild' */
	while (1)
	{
		fd_set infds;
		struct timeval timeout;
		int fromchild_fd;	/* 'fromchild' as a file descriptor */

		fromchild_fd = fileno(fromchild);
		FD_ZERO(&infds);
		FD_SET(fromchild_fd, &infds);

		/* Don't wait for anything. Just poll the file descriptor */
		timeout.tv_sec = 0;
		timeout.tv_usec = 0;

		SYNC_TRACE(5)
			fprintf(stderr, "run_conduit: flushing 'fromchild'\n");
		err = select(fromchild_fd+1,
			     &infds, NULL, NULL,
			     &timeout);

		if (err < 0)
		{
			/* An error occurred. I don't think we care */
			SYNC_TRACE(5)
				perror("select");
			break;
		}

		if (err == 0)
			/* Nothing was printed to 'fromchild' */
			break;

		/* If we get here, then something was printed to 'fromchild' */
		err = cond_readstatus(fromchild);
		if (err > 0)
			laststatus = err;
		else
			break;
	}

	/* Restore previous SIGCHLD handler */
	signal(SIGCHLD, old_sigchld);

	SYNC_TRACE(4)
		fprintf(stderr, "Closing child's file descriptors.\n");
	if (tochild != NULL)
	{
		SYNC_TRACE(7)
			fprintf(stderr, "- Closing fd %d\n", fileno(tochild));
		fpurge(tochild);
		fclose(tochild);
	}
	if (fromchild != NULL)
	{
		SYNC_TRACE(7)
			fprintf(stderr, "- Closing fd %d\n",
				fileno(fromchild));
		fpurge(fromchild);
		fclose(fromchild);
	}

	return laststatus;
}

/* run_conduits
 * This function encapsulates the common parts of the run_*_conduits()
 * functions. It takes the dlp_dbinfo for a database and a list of conduit
 * descriptors, and runs all of the ones that match.
 *
 * Returns 0 if successful, or a negative value in case of error.
 */
static int
run_conduits(struct dlp_dbinfo *dbinfo,
	     char *flavor,		/* Dump flavor: will be sent to
					 * conduit.
					 */
	     unsigned short flavor_mask)
{
	int err;
	conduit_block *conduit;
	conduit_block *def_conduit;	/* Default conduit */
	Bool found_conduit;		/* Set to true if a "real" (not
					 * just a default) matching conduit
					 * was found.
					 */

	def_conduit = NULL;		/* No default conduit yet */
	found_conduit = False;

	/* Walk the queue */
	for (conduit = config.conduits;
	     conduit != NULL;
	     conduit = conduit->next)
	{
		SYNC_TRACE(3)
			fprintf(stderr, "Trying conduit %s...\n",
				(conduit->path == NULL ? "(null)" :
				 conduit->path));

		/* See if the flavor matches */
		if ((conduit->flavors & flavor_mask) == 0)
		{
			SYNC_TRACE(5)
				fprintf(stderr, "  Flavor set 0x%02x doesn't "
					"match 0x%x\n\t=>Not applicable.\n",
					conduit->flavors,
					flavor_mask);
			continue;
		}

		/* See if the creator matches */
		if ((conduit->dbcreator != 0) &&
		    (conduit->dbcreator != dbinfo->creator))
		{
			SYNC_TRACE(5)
				fprintf(stderr,
					"  Creator: database is 0x%08lx "
					"(%c%c%c%c), conduit is 0x%08lx "
					"(%c%c%c%c).\n\t=>Not applicable.\n",
					dbinfo->creator,
					(char) ((dbinfo->creator >>24) & 0xff),
					(char) ((dbinfo->creator >>16) & 0xff),
					(char) ((dbinfo->creator >> 8) & 0xff),
					(char) (dbinfo->creator & 0xff),
					conduit->dbcreator,
					(char) ((conduit->dbcreator>>24)&0xff),
					(char) ((conduit->dbcreator>>16)&0xff),
					(char) ((conduit->dbcreator>> 8)&0xff),
					(char) (conduit->dbcreator & 0xff));
			continue;
		}

		/* See if the type matches */
		if ((conduit->dbtype != 0) &&
		    (conduit->dbtype != dbinfo->type))
		{
			SYNC_TRACE(5)
				fprintf(stderr,
					"  Type: database is 0x%08lx "
					"(%c%c%c%c), conduit is 0x%08lx "
					"(%c%c%c%c).\n\t=>Not applicable.\n",
					dbinfo->type,
					(char) ((dbinfo->type >>24) & 0xff),
					(char) ((dbinfo->type >>16) & 0xff),
					(char) ((dbinfo->type >> 8) & 0xff),
					(char) (dbinfo->type & 0xff),
					conduit->dbtype,
					(char) ((conduit->dbtype >>24) & 0xff),
					(char) ((conduit->dbtype >>16) & 0xff),
					(char) ((conduit->dbtype >> 8) & 0xff),
					(char) (conduit->dbtype & 0xff));
			continue;
		}

		/* This conduit matches */
		SYNC_TRACE(2)
			fprintf(stderr, "  This conduit matches. "
				"Running \"%s\"\n",
				(conduit->path == NULL ? "(null)" :
				 conduit->path));

		if (conduit->flags & CONDFL_DEFAULT)
		{
			SYNC_TRACE(2)
				fprintf(stderr, "  This is a default conduit. "
					"Remembering for later.\n");

			/* Remember this conduit as the default if no other
			 * conduits match.
			 */
			def_conduit = conduit;
			continue;
		}

		found_conduit = True;
		err = run_conduit(dbinfo, flavor, conduit);

		/* XXX - Error-checking. Report the conduit's exit status
		 */

		/* If this is a final conduit, don't look any further. */
		if (conduit->flags & CONDFL_FINAL)
		{
			SYNC_TRACE(2)
				fprintf(stderr, "  This is a final conduit. "
					"Not looking any further.\n");

			return 0;
		}
	}

	if ((!found_conduit) && (def_conduit != NULL))
	{
		/* No matching conduit was found, but there's a default.
		 * Run it now.
		 */
		SYNC_TRACE(4)
			fprintf(stderr, "Running default conduit\n");

		err = run_conduit(dbinfo, flavor, def_conduit);
	}

	return 0;
}

/* run_Fetch_conduits
 * Go through the list of Fetch conduits and run whichever ones are
 * applicable for the database 'dbinfo'.
 */
int
run_Fetch_conduits(struct dlp_dbinfo *dbinfo)
{
	SYNC_TRACE(1)
		fprintf(stderr, "Running pre-fetch conduits for \"%s\".\n",
			dbinfo->name);

	/* Note: If there are any open file descriptors that the conduit
	 * shouldn't have access to (other than stdin and stdout, which are
	 * handled separately, here would be a good place to close them.
	 * Use
	 *	fcntl(fd, F_SETFD, FD_CLOEXEC);
	 * The global variable 'sys_maxfds' holds the size of the file
	 * descriptor table.
	 */

	return run_conduits(dbinfo, "fetch", FLAVORFL_FETCH);
}

/* run_Dump_conduits
 * Go through the list of Dump conduits and run whichever ones are
 * applicable for the database 'dbinfo'.
 *
 * NB: this doesn't take a struct Palm argument, because the Dump conduits
 * run after the main sync has completed. Then again, the struct Palm
 * information is still lying around, and contains goodies like the PalmOS
 * version, which may be useful, so it might be a good idea to reinstate
 * it.
 */
int
run_Dump_conduits(struct dlp_dbinfo *dbinfo)
{
	SYNC_TRACE(1)
		fprintf(stderr, "Running post-dump conduits for \"%s\".\n",
			dbinfo->name);

	/* Note: If there are any open file descriptors that the conduit
	 * shouldn't have access to (other than stdin and stdout, which are
	 * handled separately, here would be a good place to close them.
	 * Use
	 *	fcntl(fd, F_SETFD, FD_CLOEXEC);
	 * The global variable 'sys_maxfds' holds the size of the file
	 * descriptor table.
	 */

	return run_conduits(dbinfo, "dump", FLAVORFL_DUMP);
}

/* run_Sync_conduits
 * Go through the list of Sync conduits and run whichever ones are
 * applicable for the database 'dbinfo'.
 *
 * XXX - This is just an experimental first draft so far
 */
int
run_Sync_conduits(struct dlp_dbinfo *dbinfo)
{
	SYNC_TRACE(1)
		fprintf(stderr, "Running sync conduits for \"%s\".\n",
			dbinfo->name);

	/* Note: If there are any open file descriptors that the conduit
	 * shouldn't have access to (other than stdin and stdout, which are
	 * handled separately, here would be a good place to close them.
	 * Use
	 *	fcntl(fd, F_SETFD, FD_CLOEXEC);
	 * The global variable 'sys_maxfds' holds the size of the file
	 * descriptor table.
	 */

	return run_conduits(dbinfo, "dump", FLAVORFL_SYNC);
}

/* spawn_conduit
 * Spawn a conduit. Runs the program named by 'path', passing it the
 * command-line arguments (including argv[0], the name of the program)
 * given by 'argv'.
 *
 * spawn_conduit() opens file handles connected to the conduit's stdin and
 * stdout, and returns these as *tochild and *fromchild, respectively. The
 * conduit's stdout is set to be line-buffered. The conduit's stdin has the
 * default buffering. The conduit's stderr remains untouched: it goes to
 * the display, or wherever you've redirected it.
 *
 * In the child, all file descriptors are closed, except for stdin, stdout,
 * stderr, and any that are set in 'openfds'. This is so that Sync conduits
 * have a file descriptor to the Palm, but Fetch and Dump conduits don't.
 *
 * If the conduit is successfully started, spawn_conduit() returns the pid
 * of the conduit process, or a negative value otherwise.
 */
static pid_t
spawn_conduit(
	const char *path,	/* Path to program to run */
	char * const argv[],	/* Child's command-line arguments */
	FILE **tochild,		/* File descriptor to child's stdin */
	FILE **fromchild,	/* File descriptor to child's stdout */
	const fd_set *openfds)	/* Set of other file descriptors that
				 * should remain open.
				 */
{
	int err;
	int inpipe[2];		/* Pipe for child's stdin */
	int outpipe[2];		/* Pipe for child's stdout */
	FILE *fh;		/* Temporary file handle */
	sigset_t sigmask;	/* Mask of signals to block */
	sigset_t old_sigmask;	/* Old signal mask */

	/* Set up the pipes for communication with the child */
	/* Child's stdin */
	if ((err = pipe(inpipe)) < 0)
	{
		perror("pipe(inpipe)");
		exit(1);
	}
	SYNC_TRACE(6)
		fprintf(stderr, "spawn_conduit: inpipe == %d, %d\n",
			inpipe[0], inpipe[1]);

	/* Turn this file descriptor into a file handle */
	SYNC_TRACE(5)
		fprintf(stderr, "spawn_conduit: tochild fd == %d\n",
			inpipe[1]);

	if ((fh = fdopen(inpipe[1], "w")) == NULL)
	{
		fprintf(stderr,
			_("%s: Can't create file handle to child's stdin\n"),
			"spawn_conduit");
		perror("fdopen");
		return -1;
	}
	*tochild = fh;

	/* Child's stdout */
	if ((err = pipe(outpipe)) < 0)
	{
		perror("pipe(outpipe)");
		exit(1);
	}
	SYNC_TRACE(6)
		fprintf(stderr, "spawn_conduit: outpipe == %d, %d\n",
			outpipe[0], outpipe[1]);
	SYNC_TRACE(5)
		fprintf(stderr, "spawn_conduit: fromchild fd == %d\n",
			outpipe[0]);

	if ((fh = fdopen(outpipe[0], "r")) == NULL)
	{
		fprintf(stderr,
			_("%s: Can't create file handle to child's stdout\n"),
			"spawn_conduit");
		perror("fdopen");
		return -1;
	}

	/* Make the child's stdout be line-buffered. This is because the
	 * child will be reporting status back on stdout, so we want to
	 * hear about it as soon as it happens.
	 */
	err = setvbuf(fh, cond_stdout_buf, _IOLBF, sizeof(cond_stdout_buf));
	if (err < 0)
	{
		fprintf(stderr,
			_("%s: Can't make child's stdout be line-buffered\n"),
			"spawn_conduit");
		perror("setvbuf");
		return -1;
	}
	*fromchild = fh;

	/* Here begins the critical section. The conduit might die
	 * immediately (e.g., the file doesn't exist, or isn't executable,
	 * or dumps core immediately). However, we don't want to get a
	 * SIGCHLD before spawn_conduit() has finished cleaning up from the
	 * fork(). Hence, we block SIGCHLD until we're done.
	 */
	sigemptyset(&sigmask);
	sigaddset(&sigmask, SIGCHLD);
	sigprocmask(SIG_BLOCK, &sigmask, &old_sigmask);

	if ((conduit_pid = fork()) < 0)
	{
		perror("fork");
		return -1;
	} else if (conduit_pid != 0)
	{
		/* This is the parent */
		SYNC_TRACE(5)
			fprintf(stderr, "Conduit PID == %d\n", conduit_pid);

		/* Close the unused ends of the pipes */
		close(inpipe[0]);
		close(outpipe[1]);

		/* Here ends the critical section of the parent. Unblock
		 * SIGCHLD.
		 */
		sigprocmask(SIG_SETMASK, &old_sigmask, NULL);

		return conduit_pid;
	}

	/* This is the child */

	/* Close the unused ends of the pipes */
	close(inpipe[1]);
	close(outpipe[0]);

	/* We don't close stdin and stdout because dup2() already does so,
	 * but does it atomically.
	 */

	/* Dup stdin to the pipe */
	if ((err = dup2(inpipe[0], STDIN_FILENO)) < 0)
	{
		perror("dup2(stdin)");
		exit(1);
	}
	close(inpipe[0]);

	/* Dup stdout to the pipe */
	if ((err = dup2(outpipe[1], STDOUT_FILENO)) < 0)
	{
		perror("dup2(stdout)");
		exit(1);
	}
	close(outpipe[1]);

	/* Unblock SIGCHLD in the child as well. */
	sigprocmask(SIG_SETMASK, &old_sigmask, NULL);

	err = execvp(path, argv);

	/* If we ever get to this point, then something went wrong */
	fprintf(stderr, _("%s: execvp(%s) failed and returned %d\n"),
		"spawn_conduit",
		path, err);
	perror("execvp");
	exit(1);		/* If we get this far, then something
				 * went wrong.
				 */
}

/* cond_readline
 * Read a line of input from the conduit. Data from the conduit, up to the
 * first newline (inclusive). The conduit's data is assumed to be ASCII; in
 * particular, it may not contain NULs.
 * XXX - Should arbitrary data be allowed?
 * Returns the number of characters read, if successful. Returns 0 in case
 * of end of file, or if the conduit exited with status 0. Returns a
 * negative value in case of error, or if the conduit exited with a status
 * other than 0, or because of a signal.
 * XXX - Make it possible to differentiate all these cases.
 */
/* XXX - EOF and the conduit exiting with status 0 should be treated
 * identically, since the conduit is expected to print a status message
 * immediately before exiting. Any conduit that closes its stdout before
 * exiting gets what it deserves.
 */
static int
cond_readline(char *buf,	/* Buffer to read into */
	      int len,		/* Max # characters to read */
	      FILE *fromchild)	/* File descriptor to child's stdout */
{
	int err;
	fd_set infds;		/* File descriptors we'll read from */
	int fromchild_fd;	/* File descriptor corresponding to
				 * 'fromchild' */

	if (fromchild == NULL)	/* Sanity check */
		return -1;

	fromchild_fd = fileno(fromchild);

	while (1)
	{
		/* Set things up for select() */
		FD_ZERO(&infds);
		FD_SET(fromchild_fd, &infds);

		SYNC_TRACE(5)
			fprintf(stderr, "cond_readline: About to select()\n");

		err = select(fromchild_fd+1, &infds, NULL, NULL, NULL);

		SYNC_TRACE(5)
			fprintf(stderr,
				"cond_readline: select() returned %d\n",
				err);


		/* Several things may have happened at this point: err < 0:
		 * means that select() was interrupted (maybe by SIGCHLD,
		 * maybe not); the conduit may or may not still be running,
		 * and it may or may not have printed anything.
		 *
		 * err == 0: This should never happen.
		 *
		 * err > 0: there are 'err' ready file descriptors. From
		 * experimenting, it looks as if the conduit may or may not
		 * be running. It may have exited before the select().
		 */

		if (err < 0)
		{
			struct timeval zerotime;
					/* Used to make select() poll */

			if (errno != EINTR)
			{
				/* Something happened other than select()
				 * being interrupted by a signal. What the
				 * hell happened?
				 */
				fprintf(stderr,
					_("%s: select() returned an "
					  "unexpected error. This should "
					  "never happen\n"),
					"cond_readline");
				perror("select");
				return -1;
			}

			/* select() was interrupted */
			if (conduit_pid > 0)
			{
				SYNC_TRACE(5)
					fprintf(stderr,
						"cond_readline: select() just "
						"got spooked, is all.\n");
				continue;
			}

			/* Poll the child to see if it printed something
			 * before exiting.
			 */
			zerotime.tv_sec = 0;
			zerotime.tv_usec = 0;
			FD_ZERO(&infds);
			FD_SET(fromchild_fd, &infds);

			SYNC_TRACE(6)
				fprintf(stderr, "cond_readline: About to "
					"check for dying message\n");

			err = select(fromchild_fd+1, &infds, NULL, NULL,
				     &zerotime);
			if (!FD_ISSET(fromchild_fd, &infds))
				goto abort;
			/* Otherwise, if the child has printed something,
			 * fall through to the next case.
			 */
		} else if (err == 0)
		{
			/* This should only ever happen if select() times
			 * out.
			 */
			fprintf(stderr, _("%s: select() returned 0. This "
					  "should never happen. Please "
					  "notify the maintainer.\n"),
				"cond_sendline");
			return -1;
		}

		/* See if the child has printed something. */
		if (FD_ISSET(fromchild_fd, &infds))
		{
			char *s;
			int s_len;	/* Length of string read */

			SYNC_TRACE(6)
				fprintf(stderr,
					"Child has written something\n");

			s = fgets(buf, len, fromchild);
			if (s == NULL)
			{
				SYNC_TRACE(6)
					fprintf(stderr, "cond_readline: "
						"fgets() returned NULL\n");

				/* Error or end of file */
				if (feof(fromchild))
				{
					SYNC_TRACE(6)
						fprintf(stderr,
							"cond_readline: "
							"End of file\n");

					/* End of file */
					return 0;
				}

				/* File error */
				SYNC_TRACE(4)
					fprintf(stderr, "cond_readline: "
						"error in fgets()\n");
				perror("cond_readline: fgets");

				return -1;
			}

			s_len = strlen(buf);
			/* XXX - If fgets() didn't read a full line, but
			 * read fewer than 'len' characters, ought to loop
			 * back and try again. Watch out, though: the child
			 * may have already exited.
			 */
			return s_len;
		}

		/* Can't get here */
		fprintf(stderr,
			_("%s: Conduit is running happily, but hasn't printed "
			  "anything. \n"
			  "And yet, I was notified. This is a bug. Please "
			  "notify the maintainer.\n"),
			"cond_readline");
	}

  abort:
	/* The conduit is dead. This is bad. */
	fprintf(stderr, _("%s: conduit exited unexpectedly\n"),
		"cond_sendline");

	SYNC_TRACE(2)
	{
		if (WIFEXITED(conduit_status))
			/* XXX - If conduit_status != 0, this fact ought to
			 * be reported to the user.
			 */
			fprintf(stderr, "Conduit exited with status %d%s\n",
				WEXITSTATUS(conduit_status),
				(WCOREDUMP(conduit_status) ?
				 " (core dumped)" : ""));
		else if (WIFSIGNALED(conduit_status))
			fprintf(stderr, "Conduit killed by signal %d%s\n",
				WTERMSIG(conduit_status),
				(WCOREDUMP(conduit_status) ?
				 " (core dumped)" : ""));
		else
			fprintf(stderr, "I have no idea "
				"how this happened\n");
	}

	if (WIFEXITED(conduit_status) &&
	    (WEXITSTATUS(conduit_status) == 0))
		/* Conduit exited normally */
		return 0;

	return -1;
}

/* cond_readstatus
 * Read a status line from the child, and process it. 'fromchild' is
 * assumed to have already been found to be readable. The line is expected
 * to be of the form
 *	\d{3}[- ].*
 * That is, it starts with a three-digit status code; then follows either a
 * space or a dash; then a text message for humans' benefit. ColdSync
 * ignores the text message; only the numeric code is significant.
 *
 * For now, conduits should use a space after the status code; eventually,
 * a dash may come to mean tht there's a continuation line.
 *
 * Error codes have the following meanings:
 *	0yz - Debugging messages. These will not normally be logged.
 *	1yz - Informational messages. These may be logged, or shown to the
 *	      user.
 *	2yz - Success.
 *	3yz - Warning
 *	4yz - ColdSync (caller) Error
 *	5yz - Conduit Error
 *
 * Returns 0 at end of file, or -1 in case of error. Otherwise, returns the
 * error code; if none was given (i.e., the line doesn't match the pattern
 * given above), assume an error code of 501.
 */
static int
cond_readstatus(FILE *fromchild)
{
	int err;			/* Internal error status */
	int errcode;			/* Error code */
	char *errmsg;			/* Error message, for humans */
	int msglen;			/* Length of 'errmsg' */
	static char buf[COND_MAXLINELEN+1];	/* Input buffer */

	/* Read a line from the child */
	err = cond_readline(buf, COND_MAXLINELEN, fromchild);
	if (err < 0)
	{
		/* Error in cond_readline(), or child exited unexpectedly */
		/* XXX - Differentiate these cases */
		SYNC_TRACE(3)
			fprintf(stderr, "cond_readstatus: Child exited "
				"unexpectedly(?)\n");
		return -1;
	} else if (err == 0)
	{
		/* End of file, or child exited normally */
		SYNC_TRACE(3)
			fprintf(stderr, "cond_readstatus: Child exited "
				"normally\n");
		return 0;
	}

	/* Chop off the trailing \n, if any */
	msglen = strlen(buf);
		/* XXX - Is 'buf' guaranteed to be NUL-terminated? */
	if (buf[msglen-1] == '\n')
		buf[msglen-1] = '\0';

	SYNC_TRACE(5)
		fprintf(stderr, "cond_readstatus: <<< \"%s\"\n", buf);

	/* See if the line is of the correct form */
	if ((msglen >= 4) &&
	    isdigit((int) buf[0]) &&
	    isdigit((int) buf[1]) &&
	    isdigit((int) buf[2]) &&
	    ((buf[3] == ' ') || (buf[3] == '-')))
	{
		/* The line matches. Extract the relevant information */
		errcode = ((buf[0] - '0') * 100) +
			((buf[1] - '0') * 10) +
			(buf[2] - '0');
		errmsg = buf+4;
	} else {
		/* The line doesn't match. Assume that the error code is
		 * 501, and that the entire line is the error message.
		 */
		errcode = 501;
		errmsg = buf;
	}

	/* XXX - Do something intelligent */

	return errcode; 
}

/* sigchld_handler
 * Handler for SIGCHLD signal. It records the state of a child as soon as
 * it changes; that way, we have up-to-date information in other parts of
 * the code, and avoid nasty timing bugs.
 */
/* XXX - I think there are OSes that reset the signal handler to its
 * default value once a signal has been received. Find out about this, and
 * cope with it.
 */
static RETSIGTYPE
sigchld_handler(int sig)
{
	pid_t p;	/* Temporary variable. Return value from waitpid() */

	MISC_TRACE(4)
		fprintf(stderr, "Got a SIGCHLD\n");

	if (canjump != 1)
	{
		/* Unexpected signal. Ignore it */
		SYNC_TRACE(5)
			fprintf(stderr, "Unexpected signal. Ignoring.\n");
		return;
	}

	if (conduit_pid <= 0)
	{
		/* Got a SIGCHLD, but there's no conduit currently running.
		 * This shouldn't happen, but it's not our responsibility.
		 * I guess this might happen if a conduit spawns a
		 * background process, then exits, and then the background
		 * process exits.
		 */
		MISC_TRACE(5)
			fprintf(stderr,
				"Got a SIGCHLD, but conduit_pid == %d. "
				"Ignoring.\n", conduit_pid);
		return;
	}

	/* Find out what just happened */
	/* XXX - WUNTRACED not specified. This means we won't find out
	 * about stopped processes. Is this a Bad Thing?
	 */
	p = waitpid(conduit_pid, &conduit_status, WNOHANG);
	if (p < 0)
	{
		fprintf(stderr, _("%s: Can't get child process status\n"),
			"sigchld_handler");
		perror("waitpid");

		conduit_pid = -1;

		SYNC_TRACE(4)
			fprintf(stderr, "siglongjmp(1)ing out of SIGCHLD.\n");
		siglongjmp(chld_jmpbuf, 1);
	}

	/* Find out whether the conduit process is still running */
	/* XXX - If WUNTRACED is deemed to be a good thing, we'll need to
	 * check WIFSTOPPED here as well.
	 */
	if (WIFEXITED(conduit_status) ||
	    WIFSIGNALED(conduit_status))
	{
		MISC_TRACE(4)
			fprintf(stderr, "Conduit is no longer running.\n");
		MISC_TRACE(5)
		{
			if (WIFEXITED(conduit_status))
				fprintf(stderr,
					"Conduit exited with status %d\n",
					WEXITSTATUS(conduit_status));
			else if (WIFSIGNALED(conduit_status))
				fprintf(stderr,
					"Conduit killed by signal %d%s\n",
					WTERMSIG(conduit_status),
					(WCOREDUMP(conduit_status) ?
					 " (core dumped)" : ""));
		}

		conduit_pid = -1;

		SYNC_TRACE(4)
			fprintf(stderr, "siglongjmp(1)ing out of SIGCHLD.\n");
		siglongjmp(chld_jmpbuf, 1);
	}
	MISC_TRACE(5)
		fprintf(stderr, "Conduit is still running.\n");

	return;		/* Nothing to do */
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
