/* conduit.c
 *
 * Functions for dealing with conduits: looking them up, loading
 * libraries, invoking them, etc.
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: conduit.c,v 1.9 1999-12-05 15:43:35 arensb Exp $
 */
/* XXX - At some point, the API for built-in conduits should become much
 * simpler. A lot of the crap in this file will disappear, since it's
 * intended to handle an obsolete conduit model. Presumably, there'll just
 * be a single table that maps an internal conduit name (e.g., "<Generic>")
 * to the function that implements it.
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <sys/types.h>			/* For pid_t, for select() */
#include <sys/time.h>			/* For select() */
#include <sys/wait.h>			/* For waitpid() */
#include <unistd.h>			/* For select() */
#include <errno.h>			/* For errno. Duh */

#if HAVE_STRINGS_H
#  include <strings.h>			/* For bzero() under AIX */
#endif	/* HAVE_STRINGS_H */

#if  HAVE_SYS_SELECT_H
#  include <sys/select.h>		/* To make select() work rationally
					 * under AIX */
#endif	/* HAVE_SYS_SELECT_H */

#if HAVE_LIBINTL
#  include <libintl.h>			/* For i18n */
#endif	/* HAVE_LIBINTL */

#include "conduit.h"

extern int run_GenericConduit(struct PConnection *pconn,
			      struct Palm *palm,
			      struct dlp_dbinfo *db);
struct conduit_spec *getconduitbyname(const char *name);
static pid_t spawn_conduit(const char *path,
			   char * const argv[],
			   char * const envp[],
			   int *tochild,
			   int *fromchild);
/* XXX - There should be two functions: cond_sendline() should send an
 * arbitrary, formatted chunk of data to the child. Some other function
 * should take a CGI-like parameter and its value, format them into a line,
 * and use cond_sendline() to send them to the child.
 */
static int cond_sendline(const char *msg,
			 int tochild,
			 int fromchild);
static RETSIGTYPE sigchld_handler(int sig);

/* XXX - This static array is bogus: it should be dynamically grown.
 * In fact, this should probably be a hash table, keyed by conduit
 * name.
 */
static struct conduit_spec *cond_pool[128];
				/* Pool of all available conduits */
/* XXX - This will be used later. Currently turned off to keep gcc
 * from fussing.
 */
/*static int cond_poolsize = sizeof(cond_pool) / sizeof(cond_pool[0]);*/
				/* Size of conduit pool */
static int num_conduits = 0;	/* Total number of conduits available */

/* XXX - This static array is bogus: it should be dynamically grown.
 */
static struct conduit_config cond_list[128];
				/* List of registered conduits: each
				 * database to be synced will be
				 * looked up in this array.
				 */
static int cond_list_len = 0;	/* # elements in cond_list */

/* Initial values for conduit pool */
static struct conduit_spec init_conduit_list[] = {
	{ "Generic C++", "", 0L, 0L, run_GenericConduit, },
};
static const int init_conduit_list_len = 
	sizeof(init_conduit_list) / sizeof(init_conduit_list[0]);

/* The following variables describe the state of the conduit process. They
 * are global variables because this data is set and used in different
 * functions.
 * Currently, ColdSync only spawns one child at a time, so there is only
 * one of each. When and if it becomes possible to spawn multiple children
 * at a time, this will need to be updated.
 */
static Bool conduit_is_running = False;	/* Is the child process currently
					 * running? */
static pid_t conduit_pid = 0;		/* The PID of the currently-running
					 * conduit.
					 */
static int conduit_status = 0;		/* Last known status of the
					 * conduit, as set by waitpid().
					 */

/* init_conduits
 * Initialize everything conduit-related.
 */
int
init_conduits(struct Palm *palm)	/* XXX - Unused argument */
{
	int i;

	/* XXX */
	for (i = 0; i < init_conduit_list_len; i++)
		load_conduit(&init_conduit_list[i]);
/*  	register_conduit("Generic", NULL, 0L, 0L, False); */
/*  	register_conduit("Memo", "MemoDB", 0L, 0L, True); */
	register_conduit("Generic C++", NULL, 0L, 0L, False);
	return 0;
}

/* tini_conduits
 * Clean up everything conduit-related.
 */
int
tini_conduits()
{
	/* XXX */
	return 0;
}

/* load_conduit
 * Add a conduit to the pool.
 */
int
load_conduit(struct conduit_spec *spec)
{
	/* XXX - This will need to be redone when cond_pool is
	 * implemented properly.
	 */
	cond_pool[num_conduits] = spec;
	num_conduits++;

	return 0;
}

struct conduit_spec *
getconduitbyname(const char *name)
{
	int i;

	if (name == NULL)
		return NULL;

/*  fprintf(stderr, "getconduitbyname: num_conduits == %d\n", num_conduits); */
	for (i = 0; i < num_conduits; i++)
	{
/*  fprintf(stderr, "getconduitbyname: checking %d\n", i); */
		if (strcmp(name, cond_pool[i]->name) == 0)
		{
/*  fprintf(stderr, "getconduitbyname(\"%s\"): found %d\n", name, i); */
			/* Found it */
			return cond_pool[i];
		}
	}

	return NULL;		/* Couldn't find it */
}

/* register_conduit
 * Add a conduit to the end of the list of active conduits: when a
 * database is synced, this conduit will be checked and run, if
 * appropriate.
 * Returns 0 if successful, or -1 if the conduit could not be found,
 * or some other error occurred.
 * XXX - It should be possible to specify that this conduit should
 * only be used for certain databases.
 */
int
register_conduit(const char *name,
		 const char *dbname,
		 const udword dbtype,
		 const udword dbcreator,
		 const Bool mandatory)
{
	struct conduit_spec *spec;

	spec = getconduitbyname(name);

	if (spec == NULL)
	{
		/* No conduit defined, so don't do anything to
		 * sync this database. Also, this is a special
		 * case since we can't make sure that the
		 * conduit is applicable to this database
		 * (since there _is_ no conduit).
		 */
		if (dbname == NULL)
			cond_list[cond_list_len].dbname[0] = '\0';
		else
			strncpy(cond_list[cond_list_len].dbname, dbname,
				DLPCMD_DBNAME_LEN);
		cond_list[cond_list_len].dbtype = dbtype;
		cond_list[cond_list_len].dbcreator = dbcreator;
		cond_list[cond_list_len].mandatory = mandatory;
		cond_list[cond_list_len].conduit = NULL;
		cond_list_len++;

		return 0;
	}

	/* Make sure the configuration doesn't conflict with the
	 * applicability of the conduit, as defined in its spec.
	 */

	/* Check database name */
	if ((dbname != NULL) &&
	    (dbname[0] != '\0') &&
	    (spec->dbname[0] != '\0') &&
	    (strcmp(dbname, spec->dbname) != 0))
	{
		/* Oops! Both the conduit spec and the arguments
		 * specify a database name, and they're not the same.
		 * This is an error.
		 */
		SYNC_TRACE(3)
			fprintf(stderr, "register_conduit: %s: "
				"Database name conflict: called with \"%s\", "
				"but spec says \"%s\"\n",
				spec->name,
				dbname, spec->dbname);
		return -1;
	}

	/* At most one database name was specified. Use it */
	if (spec->dbname[0] != '\0')
		strncpy(cond_list[cond_list_len].dbname, spec->dbname,
			DLPCMD_DBNAME_LEN);
	else if ((dbname != NULL) && (dbname[0] != '\0'))
		strncpy(cond_list[cond_list_len].dbname, dbname,
			DLPCMD_DBNAME_LEN);
	else
		cond_list[cond_list_len].dbname[0] = '\0';

	/* Check the database type */
	if ((dbtype != 0L) &&
	    (spec->dbtype != 0L) &&
	    (dbtype != spec->dbtype))
	{
		/* Oops! Both the conduit spec and 'config'
		 * specify a database type, and they're not
		 * the same. This is an error.
		 */
		SYNC_TRACE(3)
			fprintf(stderr, "register_conduit: %s: "
				"Database type conflict: config says 0x%08lx, "
				"but spec says 0x%08lx\n",
				spec->name,
				dbtype, spec->dbtype);
		return -1; 
	}

	/* At most one database type was specified. Use it */
	if (dbtype != 0L)
		cond_list[cond_list_len].dbtype = dbtype;
	else
		cond_list[cond_list_len].dbtype = spec->dbtype;

	/* Check the database creator */
	if ((dbcreator != 0L) &&
	    (spec->dbcreator != 0L) &&
	    (dbcreator != spec->dbcreator))
	{
		/* Oops! Both the conduit spec and 'config'
		 * specify a database creator, and they're not
		 * the same. This is an error.
		 */
		SYNC_TRACE(3)
			fprintf(stderr, "register_conduit: %s: "
				"Database creator conflict: config says "
				"0x%08lx, but spec says 0x%08lx\n",
				spec->name,
				dbcreator, spec->dbcreator);
		return -1;
	}

	/* At most one database creator was specified. Use it */
	if (dbcreator != 0L)
		cond_list[cond_list_len].dbcreator = dbcreator;
	else
		cond_list[cond_list_len].dbcreator = spec->dbcreator;

	/* Record other pertinent information */
	cond_list[cond_list_len].mandatory = mandatory;
	cond_list[cond_list_len].conduit = spec;

	SYNC_TRACE(2)
		fprintf(stderr, "Registered conduit \"%s\" "
			"for database \"%s\", type 0x%08lx, creator 0x%08lx\n",
			cond_list[cond_list_len].conduit->name,
			cond_list[cond_list_len].conduit->dbname,
			cond_list[cond_list_len].conduit->dbtype,
			cond_list[cond_list_len].conduit->dbcreator);

	cond_list_len++;	/* XXX - Potential overflow */
	return 0;
}

/* find_conduit
 * Find the conduit to run for the database 'db'. Returns a pointer to
 * the conduit spec, or NULL if no appropriate conduit was found.
 */
const struct conduit_spec *
find_conduit(const struct dlp_dbinfo *db)
{
	int i;
	struct conduit_spec *retval;

/*  fprintf(stderr, "find_conduit: looking for \"%s\"\n", db->name); */
	/* Walk the list of registered conduits */
	retval = NULL;
	for (i = 0; i < cond_list_len; i++)
	{
/*  fprintf(stderr, "Examining \"%s\" 0x%08lx/0x%08lx\n",
	cond_list[i].dbname, cond_list[i].dbtype, cond_list[i].dbcreator);*/
		if ((cond_list[i].dbname[0] != '\0') &&
		    (strncmp(db->name, cond_list[i].dbname,
			     DLPCMD_DBNAME_LEN) != 0))
			/* Database names don't match */
			continue;

		if ((cond_list[i].dbtype != 0L) &&
		    (db->type != cond_list[i].dbtype))
			/* Database types don't match */
			continue;

		if ((cond_list[i].dbcreator != 0L) &&
		    (db->creator != cond_list[i].dbcreator))
			/* Database creators don't match */
			continue;

		/* Found a match */
/*  fprintf(stderr, "Found a match: i == %d\n", i); */
		retval = cond_list[i].conduit;

		if (cond_list[i].mandatory)
			/* This conduit is mandatory. Stop looking */
			return retval;
	}
/*  fprintf(stderr, "Returning %dth conduit: 0x%08lx\n", i, (long) retval); */
	return retval;		/* Return the last conduit found, if
				 * any */
}

/* run_Fetch_conduits
 * Go through the list of Fetch conduits and run whichever ones are
 * applicable for the database 'dbinfo'.
 */
int
run_Fetch_conduits(struct Palm *palm,	/* XXX - Unused argument */
		   struct dlp_dbinfo *dbinfo)
{
	conduit_block *conduit;

	SYNC_TRACE(2)
		fprintf(stderr, "Running pre-fetch conduits for \"%s\".\n",
			dbinfo->name);

	for (conduit = config.fetch_q;
	     conduit != NULL;
	     conduit = conduit->next)
	{
		SYNC_TRACE(3)
			fprintf(stderr, "Trying conduit...\n");

		/* See if the creator matches */
		if ((conduit->dbcreator != 0) &&
		    (conduit->dbcreator != dbinfo->creator))
		{
			SYNC_TRACE(5)
				fprintf(stderr,
					"  Creator: database is 0x%08lx, "
					"conduit is 0x%08lx. Not applicable.\n",
					dbinfo->creator,
					conduit->dbcreator);
			continue;
		}

		/* See if the creator matches */
		if ((conduit->dbtype != 0) &&
		    (conduit->dbtype != dbinfo->type))
		{
			SYNC_TRACE(5)
				fprintf(stderr, "  Type: database is 0x%08lx, "
					"conduit is 0x%08lx. Not applicable.\n",
					dbinfo->type,
					conduit->dbtype);
			continue;
		}

		/* This conduit matches */
		SYNC_TRACE(2)
			fprintf(stderr, "  This conduit matches. "
				"I ought to run \"%s\"\n",
				(conduit->path == NULL ? "(null)" :
				 conduit->path));
	}

	return 0;		/* XXX */
}

/* run_Dump_conduits
 * Go through the list of Dump conduits and run whichever ones are
 * applicable for the database 'dbinfo'.
 */
int
run_Dump_conduits(struct Palm *palm,	/* XXX - Unused argument */
		  struct dlp_dbinfo *dbinfo)
{
	conduit_block *conduit;
	sig_t old_sigchld;		/* Previous SIGCHLD handler */

	SYNC_TRACE(2)
		fprintf(stderr, "Running post-dump conduits for \"%s\".\n",
			dbinfo->name);

	/* Set handler for SIGCHLD, so that we can keep track of what
	 * happens to conduit child processes.
	 */
	old_sigchld = signal(SIGCHLD, sigchld_handler);
	if (old_sigchld == SIG_ERR)
	{
		fprintf(stderr, _("%s: Can't set signal handler\n"),
			"run_Dump_conduits");
		perror("signal");
		return -1;
	}

	for (conduit = config.dump_q;
	     conduit != NULL;
	     conduit = conduit->next)
	{
		pid_t pid;			/* Conduit's PID */
		static char * argv[4] = {	/* Conduit's argv */
			NULL,		/* Name */
			"conduit",
			"dump",
			NULL,		/* Terminating NULL */
		};
		/* XXX - Environment shouldn't be empty. At the very least,
		 * it should contain $HOME, $PATH, $LANG (if set). The
		 * various TeX-related environment variables can also be
		 * useful.
		 * XXX - Actually, should just pick a different exec*()
		 * function, one that inherits the environment.
		 */
		static char * envp[1] = {
			NULL,		/* Terminating NULL */
		};
		int fromchild;		/* File descriptor to child's stdout */
		int tochild;		/* File descriptor to child's stdin */

		SYNC_TRACE(3)
			fprintf(stderr, "Trying conduit...\n");

		/* See if the creator matches */
		if ((conduit->dbcreator != 0) &&
		    (conduit->dbcreator != dbinfo->creator))
		{
			SYNC_TRACE(5)
				fprintf(stderr,
					"  Creator: database is 0x%08lx, "
					"conduit is 0x%08lx. Not applicable.\n",
					dbinfo->creator,
					conduit->dbcreator);
			continue;
		}

		/* See if the creator matches */
		if ((conduit->dbtype != 0) &&
		    (conduit->dbtype != dbinfo->type))
		{
			SYNC_TRACE(5)
				fprintf(stderr,
					"  Type: database is 0x%08lx, "
					"conduit is 0x%08lx. Not applicable.\n",
					dbinfo->type,
					conduit->dbtype);
			continue;
		}

		/* This conduit matches */
		SYNC_TRACE(2)
			fprintf(stderr, "  This conduit matches. "
				"I ought to run \"%s\"\n",
				(conduit->path == NULL ? "(null)" :
				 conduit->path));

		argv[0] = conduit->path;
		pid = spawn_conduit(conduit->path,
				    argv, envp,
				    &tochild, &fromchild);
		if (pid < 0)
		{
			fprintf(stderr, "%s: Can't spawn conduit\n",
				"run_Dump_conduits");

			/* Let's hope that this isn't a fatal problem */
			continue;
		}
		/* XXX - Feed the various parameters to the child via
		 * 'tochild'.
		 */
/*  sleep(2); */
cond_sendline("Hello from parent\n",
	      tochild,
	      fromchild);
		/* XXX - Send an empty line to the child (end of input) */
		/* XXX - Read child's output. Do something intelligent with
		 * it.
		 */
	}

	/* Restore previous SIGCHLD handler */
	signal(SIGCHLD, old_sigchld);

	return 0;		/* XXX */
}

/* spawn_conduit
 * Spawn a conduit. Runs the program named by 'path', passing it the
 * command-line arguments (including argv[0], the name of the program)
 * given by 'argv', and the environment given in 'envp'. Both 'argv' and
 * 'envp' are passed unchanged to execve().
 *
 * spawn_conduit() opens file descriptors connected to the conduit's stdin
 * and stdout, and returns these as *tochild and *fromchild, respectively.
 * The conduit's stderr remains untouched.
 *
 * If the conduit is successfully started, spawn_conduit() returns the pid
 * of the conduit process, or a negative value otherwise.
 */
/* XXX - Should probably fix this to add the 'conduit' and flavor
 * arguments.
 */
static pid_t
spawn_conduit(
	const char *path,	/* Path to program to run */
	char * const argv[],	/* Child's command-line arguments */
	char *const envp[],	/* Child's environment */
	int *tochild,		/* File descriptor to child's stdin */
	int *fromchild)		/* File descriptor to child's stdout */
{
	int err;
	pid_t pid;		/* Child's PID */
	int inpipe[2];		/* Pipe for child's stdin */
	int outpipe[2];		/* Pipe for child's stdout */

	/* Set up the pipes for communication with the child */
	if ((err = pipe(inpipe)) < 0)
	{
		perror("pipe(inpipe)");
		exit(1);
	}
	*tochild = inpipe[1];

	if ((err = pipe(outpipe)) < 0)
	{
		perror("pipe(outpipe)");
		exit(1);
	}
	*fromchild = outpipe[0];

	if ((pid = vfork()) < 0)
	{
		perror("vfork");
		return -1;
	} else if (pid != 0)
	{
		/* The order of assignments here is deliberate: I'm not
		 * sure what the potential timing problems are, but I'm
		 * pretty sure it would be a Bad Thing if the child died
		 * between the two assignments: we'd then have
		 * conduit_is_running == True, but conduit_pid would have a
		 * bogus value.
		 */
		conduit_pid = pid;
		conduit_is_running = True;

		return pid;
	}

	/* This is the child */

	/* Close stdin and stdout */
	if (close(0) < 0)
	{
		perror("close(0)");
		exit(1);
	}
	if (close(1) < 0)
	{
		perror("close(1)");
		exit(1);
	}

	/* Dup stdin to the pipe */
	if ((err = dup2(inpipe[0], 0)) < 0)
	{
		perror("dup2(stdio)");
		exit(1);
	}
	close(inpipe[0]);

	/* Dup stdout to the pipe */
	if ((err = dup2(outpipe[1], 1)) < 0)
	{
		perror("dup2(stdout)");
		exit(1);
	}
	close(outpipe[1]);

	/* XXX - At this point, it may be tempting to close all of the
	 * other file descriptors as well. Then again, presumably the
	 * parent knows what it's doing.
	 * OTOH, the child currently has a file descriptor to the Palm.
	 * This may be undesirable.
	 */

	err = execve(path, argv, envp);
fprintf(stderr, "execve() returned %d\n", err);
	perror("execve");
	return -1;		/* If we get this far, then something
				 * went wrong.
				 */
}

/* cond_sendline

 * Send a string to a child. The
 * XXX
 */
/* XXX - This should be able to send arbitrary data. Hence, this should
 * take a 'len' argument to indicate the length of the string.
 */
static int
cond_sendline(const char *str,	/* String to send */
	      int tochild,	/* File descriptor to child's stdin */
	      int fromchild)	/* File descriptor to child's stdout */
{
	int err;
	fd_set infds;		/* File descriptors we'll read from */
	fd_set outfds;		/* File descriptors we'll write to */
	int max_fd;		/* Highest-numbered file descriptor,
				 * for select().
				 */

	/* Get highest file descriptor number */
	max_fd = (fromchild > tochild) ? fromchild : tochild;

	while (1)
	{
		/* Set up the file descriptors to listen on */
		FD_ZERO(&infds);
		FD_SET(fromchild, &infds);
		FD_ZERO(&outfds);
		FD_SET(tochild, &outfds);

		err = select(max_fd+1, &infds, &outfds, NULL, NULL);
fprintf(stderr, "select() returned %d\n", err);

		if (err < 0)
		{
			int status;		/* waitpid() status */

			/* select() returned an error. Barring programming
			 * error, the only reason for this should be that
			 * we received a signal before any of the file
			 * descriptors became readable or writable.
			 */
			if (errno != EINTR)
			{
				fprintf(stderr,
					_("%s: select() returned an "
					  "unexpected error.\n"),
					"cond_sendline");
				perror("select");
				return -1;
			}

			if (waitpid(conduit_pid, &status, WNOHANG) < 0)
			{
fprintf(stderr, "Can't get conduit status\n");
perror("waitpid");
				return -1; 
			}
fprintf(stderr, "waitpid() gave status 0x%04x\n", status);

			if (WIFEXITED(status))
			{
				/* The conduit exited normally. This is bad
				 * for this function, since we were
				 * expecting to write to it.
				 */
fprintf(stderr, "Conduit exited with status %d\n",
	WEXITSTATUS(status));
				return -1; 
			}

			if (WIFSIGNALED(status))
			{
				/* The conduit exited abnormally. */
fprintf(stderr, "Conduit killed by signal %d%s\n",
	WTERMSIG(status),
	(WCOREDUMP(status) ? " (core dumped)" : ""));
				return -1; 
			}
fprintf(stderr, "Conduit is okay. select() got spooked, is all\n");

			/* The conduit has not exited yet. It may be
			 * suspended, or the select() above may have been
			 * interrupted by some completely unrelated signal.
			 * At any rate, ignore it.
			 */
			/* XXX - Conduits shouldn't stop. We should
			 * probably watch for this.
			 */
			continue;
		}

		/* We didn't call select() with a timeout, so it should
		 * never return 0.
		 */

		/* See if the child has printed something. */
		if (FD_ISSET(fromchild, &infds))
		{
			static char buf[1024];
fprintf(stderr, "Child has written something\n");
			/* XXX - Read what the child has to say, and do
			 * something intelligent about it.
			 */
read(fromchild, buf, 1024);
fprintf(stderr, "<<< \"%s\"\n", buf);

			continue;	/* Try select()ing again */
		}

		/* XXX - See if the child is ready to read */
		if (FD_ISSET(tochild, &outfds))
		{
fprintf(stderr, "Child is ready to read\n");
			/* Child is ready to read a line */
			/* XXX - This should loop until all of the string
			 * has been sent.
			 */
fprintf(stderr, ">>> [%s]\n", str);
			err = write(tochild, str, strlen(str));
			if (err < 0)
			{
				fprintf(stderr, _("%s: write() failed\n"),
					"cond_sendline");
				perror("write");
				return -1;
			}
fprintf(stderr, "Wrote okay\n");
			return 0;	/* Success */
		}
	}

	/* XXX - Return something intelligent */
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
#if 0
	pid_t p;

	MISC_TRACE(4)
		fprintf(stderr, "Got a SIGCHLD\n");

	if (!conduit_is_running)
	{
		/* Got a SIGCHLD, but there's no conduit currently running.
		 * This shouldn't happen, but it's not our responsibility.
		 * I guess this might happen if a conduit spawns a
		 * background process, then exits, and then the background
		 * process exits.
		 * XXX - Actually, will ignoring it create zombies?
		 */
		MISC_TRACE(5)
			fprintf(stderr,
				"Got a SIGCHLD, but I have no children. "
				"Ignoring.\n");
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

		conduit_is_running = False;
		return;
	}

	/* Find out whether the conduit process is still running */
	/* XXX - If WUNTRACED is determined to be a good thing, we'll need
	 * to check WIFSTOPPED here as well.
	 */
	if (WIFEXITED(conduit_status) ||
	    WIFSIGNALED(conduit_status))
	{
		MISC_TRACE(5)
			fprintf(stderr, "Conduit is no longer running.\n");

		conduit_is_running = False;
		return;
	}
	MISC_TRACE(5)
		fprintf(stderr, "Conduit is still running.\n");
#endif	/* 0 */
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
