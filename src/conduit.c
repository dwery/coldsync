/* conduit.c
 *
 * Functions for dealing with conduits: looking them up, loading
 * libraries, invoking them, etc.
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id$
 */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>			/* For malloc(), free() */
#include <string.h>
#include <libgen.h>			/* For dirname() */
#include <pwd.h>			/* For getpwuid() */
#include <sys/types.h>			/* For pid_t, for select(); write() */
#include <sys/uio.h>			/* For write() */
#include <sys/time.h>			/* For select() */
#include <sys/wait.h>			/* For waitpid() */
#include <sys/socket.h>			/* For socketpair() */

#if HAVE_SYS_PARAM_H
#  include <sys/param.h>		/* For ntohs() and friends */
#endif	/* HAVE_SYS_PARAM_H */
#if HAVE_NETINET_IN_H
#  include <netinet/in.h>		/* For ntohs() and friends, under
					 * Linux */
#endif	/* HAVE_NETINET_IN_H */

#include <unistd.h>			/* For select(), write(), access() */
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
#  define WCOREDUMP(status)	0
#endif	/* WCOREDUMP */

#include "conduit.h"
#include "spc.h"
#include "pref.h"
#include "cs_error.h"
#include "symboltable.h"

#include "conduits.h"

#define MAX_SANE_FD	32	/* Highest-numbered file descriptor one
				 * might get in a sane universe. Anything
				 * higher than this means that there's a
				 * file descriptor leak.
				 */

typedef RETSIGTYPE (*sighandler) (int);	/* This is equivalent to FreeBSD's
					 * 'sig_t', but that's a BSDism.
					 */

static int run_conduits(struct Palm *palm,
			const struct dlp_dbinfo *dbinfo,
			char *flavor,
			unsigned short flavor_mask,
			const Bool with_spc,
			pda_block *pda);
static const char *find_in_path(const char *conduit);
static pid_t spawn_conduit(const char *path,
                           const char *cwd,
			   char * const argv[],
			   FILE **tochild,
			   FILE **fromchild,
			   const fd_set *openfds);
static int cond_readline(char *buf,
			 int len,
			 FILE *fromchild);
static int cond_readstatus(FILE *fromchild);
static RETSIGTYPE sigchld_handler(int sig);
static INLINE Bool crea_type_matches(
	const conduit_block *cond,
	const udword creator,
	const udword type,
	const unsigned char flags);

typedef int (*ConduitFunc)(PConnection *pconn,
			   const struct dlp_dbinfo *dbinfo,
			   const conduit_block *block,
			   const pda_block *pda);

struct ConduitDef *findConduitByName(const char *name);

struct ConduitDef
{
	char *name;
	ConduitFunc func;
	unsigned short flavors;
	/* XXX - Whether this conduit handles resources, records, or both.
	 */
	/* XXX - Other stuff */
};

struct ConduitDef builtin_conduits[] = {
	{ "[dummy]", run_DummyConduit,
	  FLAVORFL_INSTALL | FLAVORFL_FETCH | FLAVORFL_DUMP | FLAVORFL_SYNC, },
	{ "[generic]", run_GenericConduit, FLAVORFL_SYNC, },
};
#define num_builtin_conduits	sizeof(builtin_conduits) / sizeof(builtin_conduits[0])

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

/* block_sigchld
 * Just a convenience function. This blocks SIGCHLD so that the current
 * process doesn't get interrupted by a signal at the wrong moment.
 *
 * The 'sigmask' argument gets filled in with a cookie that will later be
 * passed to unblock_sigchld() to unblock the signal.
 */
static INLINE void
block_sigchld(sigset_t *sigmask)
{
	sigset_t new_sigmask;

	CONDUIT_TRACE(7)
		fprintf(stderr, "Blocking SIGCHLD.\n");
	sigemptyset(&new_sigmask);
	sigaddset(&new_sigmask, SIGCHLD);
	sigprocmask(SIG_BLOCK, &new_sigmask, sigmask);
}

/* unblock_sigchld
 * Just a convenience function. Unblocks SIGCHLD by restoring the process's
 * signal mask to what it was before block_sigchld() was called.
 * The 'old_sigmask' argument is the cookie that was filled in by
 * block_sigchld().
 */
static INLINE void
unblock_sigchld(const sigset_t *old_sigmask)
{
	CONDUIT_TRACE(7)
		fprintf(stderr, "Unblocking SIGCHLD.\n");
	sigprocmask(SIG_SETMASK, old_sigmask, NULL);
}

/* poll_fd
 * Poll the file descriptor 'fd' to see if it's readable. If 'for_writing'
 * is True, check to see if 'fd' is writable.
 * Returns 1 if 'fd' is readable (or writable, if 'for_writing' is set), or
 * 0 if it isn't. Returns a negative value in case of error.
 */
static INLINE int
poll_fd(const int fd, const Bool for_writing)
{
	fd_set fds;
	struct timeval timeout;
	int err;

	/* This works simply by calling select() with a timeout of 0. That
	 * is, select() should simply see if the appropriate file
	 * descriptor is readable (or writable).
	 */
	FD_ZERO(&fds);
	FD_SET(fd, &fds);

	timeout.tv_sec  = 0;
	timeout.tv_usec = 0;

	if (for_writing)
		err = select(fd+1, NULL, &fds, NULL, &timeout);
	else
		err = select(fd+1, &fds, NULL, NULL, &timeout);

	if (err < 0)	return -1;	/* An error occurred */
	if (err == 0)	return  0;	/* File descriptor not readable */
	return 1;			/* File descriptor is readable */
}

/* The following two variables are for setvbuf's benefit, for when we make
 * the conduit's stdin and stdout be line-buffered.
 */
#if 0	/* This isn't actually used anywhere */
static char cond_stdin_buf[BUFSIZ];	/* Buffer for conduit's stdin */
#endif	/* 0 */
static char cond_stdout_buf[BUFSIZ];	/* Buffer for conduit's stdout */

/* Add a new header to list, allocating memory as needed, and copying the
 * values passed in. Returns zero on success
 */

static int
add_header(struct cond_header** headers,
	unsigned int* num_headers,
	unsigned int* max_headers,
	const char* name,
	const char* value)
{
	if (name == NULL || value == NULL)
	{
		return -1;
	}

	if (strlen(name) > COND_MAXHFIELDLEN)
	{
		Error(_("%s: Header '%s' is too long "),
				"run_conduit", name);
		return -1;
	}

	if (*headers == NULL)
	{
		*num_headers = 0;

		/* Initialize max_headers high enough that we shouldn't have to
		 * ever reallocate.
		 */

		*max_headers = 16;
		*headers = calloc(*max_headers, sizeof(struct cond_header));

	} else if (*num_headers >= *max_headers)
	{
		/* Double max_headers and realloc */

		*max_headers *= 2;
		*headers = realloc(*headers, *max_headers * sizeof(struct cond_header));
	}

	if (*headers == NULL) {
		return -1;
	}

	if (*num_headers > 0)
	{
		/* Convert to linked list as we go */
		(*headers)[*num_headers-1].next = &(*headers)[*num_headers];
	}
	(*headers)[*num_headers].name = strdup(name);
	(*headers)[*num_headers].value = strdup(value);
	(*headers)[*num_headers].next = NULL;

	CONDUIT_TRACE(6)
		fprintf(stderr, "Header[%d] %s:%s\n",
			*num_headers,
			(*headers)[*num_headers].name,
			(*headers)[*num_headers].value);

	*num_headers += 1;

	return 0;
}

static void
free_headers(struct cond_header** headers,
	unsigned int* num_headers)
{
	unsigned int i;

	if (*headers != NULL)
	{
		for (i = 0; i < *num_headers; i ++)
		{
			if ((*headers)[i].name != NULL)
				free ((*headers)[i].name);

			if ((*headers)[i].value != NULL)
				free ((*headers)[i].value);
		}
		free (*headers);
	}
	*headers = NULL;
	*num_headers = 0;
}



/* run_conduit
 * Run a single conduit, of the given flavor.
 * Returns a negative value in case of an error running the conduit.
 * Otherwise, returns the last status returned by the conduit (which may
 * indicate an error with the conduit).
 */
/* XXX - This function is rather ugly, since it tries to be everything to
 * all conduits. This, in turn, is because about half of what
 * run_conduit() does is common to all conduit flavors, so it is
 * appropriate that it be encapsulated in a function.
 * Is it possible to abstract out just the common parts, and let other
 * functions take care of the flavor types' idiosyncracies?
 */
/* XXX - The 'flavor_mask' argument is rather ugly. Perhaps the best way to
 * get rid of it is to have the various run_*_conduit() functions generate
 * their own list of headers, and pass it to run_conduit().
 */

static int
run_conduit(struct Palm *palm,
		const struct dlp_dbinfo *dbinfo,	/* The database to sync */
		char *flavor,				/* Name of the flavor */
		unsigned short flavor_mask,		/* Mask of the flavor */
		conduit_block *conduit,			/* Conduit to be run */
		const Bool with_spc,			/* Allow SPC calls? */
		pda_block *pda)
{
	int err;
	int i;
	static char * argv[4];	/* Conduit's argv */
	pid_t pid;		/* Conduit's PID */
	FILE *fromchild = NULL;	/* File handle to child's stdout */
	FILE *tochild = NULL;	/* File handle to child's stdin */
	const char *bakfname;	/* Path to backup file (backup or install) */
	volatile int laststatus = 501;
				/* The last status code printed by the
				 * child. This is used as its exit status.
				 * Defaults to 501, since that's a sane
				 * return status if the conduit doesn't run
				 * at all (e.g., it isn't executable).
				 */
	struct cond_header *hdr;	/* User-supplied header */
	sighandler old_sigchld;		/* Previous SIGCHLD handler */
	struct cond_header* headers = NULL; /* System headers */
	unsigned int num_headers = 0;		/* Number of headers in list */
	unsigned int max_headers = 0;		/* Amount of room in list */
	int spcpipe[2];		/* Pipe for SPC-based communication */
	static enum { SPC_Read_Header,  SPC_Read_Data,
		      SPC_Write_Header, SPC_Write_Data
	} spc_state;		/* This variable helps us implement a state
				 * machine, by indicating what needs to be
				 * done next:
				 *	- read the header of the next SPC
				 *	request from the conduit
				 *	- read (more of) the data of an SPC
				 *	request from the conduit
				 *	- send the SPC response header to
				 *	the conduit
				 *	- send (more of) the SPC response
				 *	data to the conduit
				 */

	struct spc_hdr spc_req;
				/* SPC request header */
	/* These next few variables are declared 'volatile' mainly to make
	 * gcc shut up: it complains that they might get clobbered by
	 * longjmp(). This is true, but we don't care.
	 * Note the order for pointers, though: it is the pointer that is
	 * volatile, not what it points to.
	 */
	unsigned char * volatile spc_inbuf = NULL;
				/* SPC input buffer */
	volatile unsigned long spc_toread = 0;
				/* # bytes of an SPC request left to read */
	unsigned char * volatile spc_outbuf = NULL;
				/* SPC output buffer */
	volatile unsigned long spc_towrite = 0;
				/* # bytes of an SPC response left to write */
	unsigned char * volatile spcp = NULL;
				/* Pointer into spc_inbuf or spc_outbuf */
	sigset_t sigmask;
				/* Signal mask for {,un}block_sigchld() */
	const struct pref_item ** volatile pref_list = NULL;
				/* Array of pointers to preference items in
				 * the cache */
	char numbuf[16]; /* big enough for a fd value or a version */


	if (conduit->path == NULL)
		/* This conduit has no path. It does nothing. This can be
		 * useful for defining a do-nothing default.
		 */
		return 201;		/* Success (trivially) */

	/* If this conduit might understand SPC, set up a pipe for the
	 * child to communicate to the parent.
	 */
	if (with_spc)
	{
		/* Tell the PDA we are running a conduit. This is useful to
		 * switch the message on the display to "Synchronizing" and to check
		 * for an abort condition.
		 */
		/* XXX - Check for the abort condition ;) */
		DlpOpenConduit(palm_pconn(palm));


		/* Set up a pair of pipes for talking SPC with the child */
		if ((err = socketpair(AF_UNIX, SOCK_STREAM, 0, spcpipe)) < 0)
		{
			Perror("pipe(spcpipe)");
			return 501;
		}

		/* XXX - Arrange for the parent end of the pipe to be
		 * closed upon exec().
		 */
		/* XXX - Ditto for the Palm file descriptor */

		/* XXX - Should this pipe be made unbuffered? (Probably
		 * not, but it'd be nice to flush it after writing.)
		 */

		spc_state = SPC_Read_Header;	/* Next thing to do */

		CONDUIT_TRACE(6)
		{
			fprintf(stderr, "spcpipe == (%d, %d)\n",
				spcpipe[0], spcpipe[1]);
		}

		/* Sanity check */
		if (spcpipe[1] > MAX_SANE_FD)
		{
			Error(_("%s: I just got file descriptor %d. "
				"There appears to be a file\n"
				"descriptor leak. Please notify the "
				"maintainer."),
			      "run_conduit", spcpipe[1]);
		}
		if (spcpipe[0] > 999)
		{
			Error(_("%s: Too many file descriptors. Aborting."),
			      "run_conduit");
			return 502;
		}
	}

	/* Set handler for SIGCHLD, so that we can keep track of what
	 * happens to conduit child processes.
	 */
	old_sigchld = signal(SIGCHLD, sigchld_handler);
	if (old_sigchld == SIG_ERR)
	{
		Error(_("%s: Can't set signal handler."),
		      "run_conduit");
		Perror("signal");
		return 503;
	}

	/* Before all the jumping stuff, make sure the pref_list is
	 * allocated.
	 */
	CONDUIT_TRACE(6)
		fprintf(stderr, "run_conduit: %d prefs in this conduit\n",
			conduit->num_prefs);
	if (conduit->num_prefs > 0)
		pref_list = calloc(conduit->num_prefs, sizeof *pref_list);

	/* When the child exits, sigchld_handler() will longjmp() back to
	 * here. This way, none of the other code has to worry about
	 * whether the conduit is still running.
	 */
	if ((err = sigsetjmp(chld_jmpbuf, 1)) != 0)
	{
		/* NB: Both FreeBSD's and the Open Group's manual entries
		 * say that longjmp() restores the environment saved by
		 * _the most recent_ invocation of setjmp().
		 *
		 * Furthermore, Stevens says that you can't call longjmp()
		 * if the function that called setjmp() has already
		 * terminated.
		 *
		 * This implies that it's okay to call setjmp() multiple
		 * times with the same jump buffer.
		 */
		CONDUIT_TRACE(6)
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
			    conduit->cwd,				
			    argv,
			    &tochild, &fromchild,
			    NULL);
	if (pid < 0)
	{
		Error(_("%s: Can't spawn conduit."),
		      "run_conduit");

		/* Let's hope that this isn't a fatal problem */
		goto abort;
	}

	/* Feed the various parameters to the child via 'tochild'. */

	/* Initialize the standard header values */
	block_sigchld(&sigmask);	/* Don't disturb me now */

	add_header(&headers, &num_headers, &max_headers, "Daemon", PACKAGE);
	add_header(&headers, &num_headers, &max_headers, "Version", VERSION);
	add_header(&headers, &num_headers, &max_headers,
		"SyncType", global_opts.force_slow || need_slow_sync ? "Slow" : "Fast");
	add_header(&headers, &num_headers, &max_headers,
		"PDA-Snum", palm_serial(palm));
	add_header(&headers, &num_headers, &max_headers,
		"PDA-Username", palm_username(palm));
	sprintf(numbuf, "%d", (int) palm_userid(palm));
	add_header(&headers, &num_headers, &max_headers,
		"PDA-UID", numbuf);

	if (pda)
	{
		add_header(&headers, &num_headers, &max_headers,
			"PDA-Directory", pda->directory);
		add_header(&headers, &num_headers, &max_headers,
			"PDA-Default", (pda->flags & PDAFL_DEFAULT) ? "1" : "0");
	}

	if (dbinfo)
	{
		/* Construct the input filename to pass to the conduit. For install
		 * conduits, this file is in ~/.palm/install/; for all other
		 * flavors, it is in ~/.palm/backup/.
		 */
		bakfname = (flavor_mask & FLAVORFL_INSTALL) ?
			mkinstfname(dbinfo) :
			mkbakfname(dbinfo);

		add_header(&headers, &num_headers, &max_headers, "InputDB", bakfname);
		add_header(&headers, &num_headers, &max_headers, "OutputDB", bakfname);
	}

	/* Then we add the preference items as headers */
	for (i = 0; i < conduit->num_prefs; i++)
	{
		static char tmpvalue[64];
				/* Since 2^64 is a 20-digit number
				 * (decimal), this should be big enough to
				 * hold the longest possible preference
				 * line.
				 */ 

		/* Set the pointer to the right preference in the cache
		 * list and if necessary, download it
		 */

		CONDUIT_TRACE(4)
			fprintf(stderr,
				"run_conduit: sending preference %d: "
				"0x%08lx/%d\n",
				i,
				conduit->prefs[i].creator,
				conduit->prefs[i].id);

		pref_list[i] = GetPrefItem(&(conduit->prefs[i]));
		if (pref_list[i] == NULL)
		{
			switch (cs_errno)
			{
			    case CSE_NOCONN:
			    case CSE_CANCEL:
				break;

			    default:
				Error(_("Can't get preference item."));
				break;
			}
			goto abort;
		}

		/* Build the preference line with sprintf() because
		 * snprintf() isn't portable.
		 */
		/* XXX - snprintf() is now included. */
		sprintf(tmpvalue, "%c%c%c%c/%d/%d",
			(char) (conduit->prefs[i].creator >> 24) & 0xff,
			(char) (conduit->prefs[i].creator >> 16) & 0xff,
			(char) (conduit->prefs[i].creator >> 8) & 0xff,
			(char) conduit->prefs[i].creator & 0xff,
			conduit->prefs[i].id,
			pref_list[i]->contents_info->len);

		add_header(&headers, &num_headers, &max_headers,
			"Preference", tmpvalue);
	}

	/* If the conduit might understand SPC, tell it what file
	 * descriptor to use to talk to the parent.
	 */
	if (with_spc)
	{
		sprintf(numbuf, "%d", spcpipe[0]);
		add_header(&headers, &num_headers, &max_headers,
			"SPCPipe", numbuf);

		/* provide DLP version numbers. We only bother when we've
		 * got SPC enabled since it's useless otherwise.
		 */
		sprintf(numbuf, "%d", palm_dlp_ver_major(palm));
		add_header(&headers, &num_headers, &max_headers,
			"PDA-DLP-major", numbuf );

		sprintf(numbuf, "%d", palm_dlp_ver_minor(palm));
		add_header(&headers, &num_headers, &max_headers,
			"PDA-DLP-minor", numbuf );
	}

	/* Now append the user-supplied headers to the system headers. This
	 * is a semi-ugly hack that allows us to treat the whole set of
	 * headers as a single list.
	 * XXX If add_header() wasn't as simple as it was, we'd be nuts
	 * trying this.
	 */
	headers[num_headers-1].next = conduit->headers;

	unblock_sigchld(&sigmask);

	/* Iterate over the list of headers */
	for (hdr = headers; hdr != NULL; hdr = hdr->next)
	{
		static char buf[COND_MAXLINELEN+2];
				/* Buffer to hold the line we're about to
				 * send. The +2 is to hold a \n and a NUL
				 * at the end.
				 */
		char *bufp;	/* Pointer to the part of 'buf' that should
				 * be written next.
				 */
		int len;	/* How many bytes of 'bufp' to write */

		/* Skip if the header value is NULL */
		if (hdr->value == NULL)
			continue;

		/* Create the header line. Make sure that the entire line
		 * is no more than COND_MAXLINELEN characters in length
		 * (not counting the \n at the end), and that the header
		 * name is no more than COND_MAXHFIELDLEN characters in
		 * length.
		 */
		snprintf(buf, COND_MAXLINELEN+1, "%.*s: %s\n",
			 COND_MAXHFIELDLEN, hdr->name,
			 hdr->value);
		bufp = buf;
		len = strlen(bufp);

	  check_status:
		/* Before proceeding, see if the child has written a status
		 * message.
		 */
		err = poll_fd(fileno(fromchild), False);
		if (err < 0)
		{
			/* An error occurred. I don't think we care at this
			 * point.
			 */
			CONDUIT_TRACE(5)
				Perror("poll_fd");
		} else if (err > 0)
		{
			/* The child has printed something. Read it, then
			 * check again.
			 */
			err = cond_readstatus(fromchild);
			if (err > 0)
				laststatus = err;
			goto check_status;
		}

		/* Okay, now that the child has nothing to say, we can send
		 * it the current header.
		 */
		CONDUIT_TRACE(4)
			fprintf(stderr, ">>> %s: %s\n",
				hdr->name,
				hdr->value);

		while (len > 0)
		{
			CONDUIT_TRACE(7)
				fprintf(stderr, "writing chunk [%s] (%d)\n",
					bufp, len);
			err = write(fileno(tochild), bufp, len);
			if (err < 0)
			{
				/* An error occurred */
				Error(_("Couldn't send header to conduit."));
				Perror("write");
				goto abort;
			}

			/* write() might not have written all of 'bufp' */
			len -= err;
			bufp += err;
		}
	}

	/* Send an empty line to the child (end of headers) */
	fprintf(tochild, "\n");

	/* Now write all the raw data to the child */
	for(i = 0; i < conduit->num_prefs; i++)
	{
		int rc = fwrite(pref_list[i]->contents,
			1,
			pref_list[i]->contents_info->len,
			tochild);
		if (rc != pref_list[i]->contents_info->len)
		{
			Error(_("Couldn't send preference to conduit."));
			goto abort;
		}
	}
	fflush(tochild);

	/* Listen for the child to either a) print a status message on its
	 * stdout, or b) send an SPC request on its SPC file descriptor.
	 */
	while (1)
	{
		fd_set in_fds;		/* Set of file descriptors to
					 * listen to */
		fd_set out_fds;		/* Set of file descriptors to
					 * write to */
		int max_fd;		/* Highest-numbered file descriptor
					 * to listen to. */

		FD_ZERO(&in_fds);
		FD_ZERO(&out_fds);
		FD_SET(fileno(fromchild), &in_fds);
		max_fd = fileno(fromchild);
		if (with_spc)
		{
			/* Depending on the state of the SPC state machine,
			 * we should either expect to read from or expect
			 * to write to the SPC pipe, but not both.
			 * This is because the SPC pipe will usually be
			 * writable even if we have nothing to write to it,
			 * and we don't want to busy-wait.
			 */
			switch (spc_state)
			{
			    case SPC_Read_Header:
			    case SPC_Read_Data:
				FD_SET(spcpipe[1], &in_fds);
				break;
			    case SPC_Write_Header:
			    case SPC_Write_Data:
				FD_SET(spcpipe[1], &out_fds);
				break;
			    default:
				Error(_("%s: Conduit handler is in "
					"an inconsistent state at\n"
					"%s, line %d. Please notify the "
					"maintainer."),
				      "run_conduit", __FILE__, __LINE__);
				goto abort;
			}

			if (spcpipe[1] > max_fd)
				max_fd = spcpipe[1];
		}

		/* XXX - This really ought to time out: a buggy conduit
		 * might not send the right amount of data, in which case
		 * this will currently hang forever.
		 * The proper timeout depends on the context, of course:
		 * for a Dump conduit, it might be okay to wait 30 seconds
		 * for status info. In SPC_Read_Data state, 1 second should
		 * be enough. Try to find some balance that's reasonable
		 * but doesn't make the user wait forever.
		 */
		err = select(max_fd+1, &in_fds, &out_fds, NULL, NULL);
		CONDUIT_TRACE(7)
			fprintf(stderr,
				"run_conduit: select() returned %d\n",
				err);

		if (err < 0)
		{
			if (errno != EINTR)
			{
				Error(_("%s: Error in select()."),
				      "run_conduit");
				Perror("select");

				/* EINTR is harmless. All of the other ways
				 * select() can return -1 are severe
				 * errors. I don't know how to continue.
				 */
				goto abort;
			}
		}
		if (err == 0)
		{
			/* This should never happen */
			Warn(_("%s: select() returned 0. I'm puzzled."),
			     "run_conduit");
			continue;
		}

		/* Check fromchild, to see if the child has printed a
		 * status message to stdout.
		 */
		if (FD_ISSET(fileno(fromchild), &in_fds))
		{
			CONDUIT_TRACE(4)
				fprintf(stderr,
					"Child has printed to stdout.\n");

			block_sigchld(&sigmask);
			err = cond_readstatus(fromchild);
			unblock_sigchld(&sigmask);

			CONDUIT_TRACE(2)
				fprintf(stderr,
					"run_conduit: got status %d\n",
					err);
			if (err <= 0)
				/* Got an end of file (or an error) */
				goto abort;

			/* cond_readstatus() got a legitimate status.
			 * Remember it for later.
			 */
			laststatus = err;
		}

		/* From here on in, everything has to do with SPC */
		if (!with_spc)
			continue;

		/* Check the state of the SPC state machine, and figure out
		 * what to do next.
		 */

		/* State 0: expecting to read a header */
		if ((spc_state == SPC_Read_Header) &&
		    FD_ISSET(spcpipe[1], &in_fds))
		{
			static unsigned char spc_header[SPC_HEADER_LEN];

			/* Read the SPC header. This consists of an opcode,
			 * a status code (ignored), and a length.
			 */
			err = read(spcpipe[1], spc_header, SPC_HEADER_LEN);
			if (err < 0)
			{
				Error(_("%s: Error reading SPC request "
					"from conduit."),
				      "run_conduit");
				Perror("read");
				/* XXX - What now? Abort? */
			}
			if (err != SPC_HEADER_LEN)
			{
				/* The child printed something, but it's
				 * not the right length for an SPC request.
				 */
				/* XXX - Send a "bad request" header to the
				 * child.
				 */
				Error(_("%s: Error header wrong length %ld."),
				      "run_conduit",err);
				Perror("read");
				goto abort;
			}

			/* Very crude parsing of the received header */
			/* XXX - There should probably be a function to
			 * parse this.
			 */
			spc_req.op = ntohs(*((unsigned short *) spc_header));
			spc_req.len = ntohl(
				* ((unsigned long *) (spc_header+4)));

			CONDUIT_TRACE(5)
				fprintf(stderr,
					"SPC request OP == %d, "
					"len == %ld\n",
					spc_req.op,
					spc_req.len);

			spc_toread = spc_req.len;
			if (spc_req.len > 0)
			{
				block_sigchld(&sigmask);

				spc_inbuf = malloc(spc_req.len);
				/* XXX - Error-checking */
				spcp = spc_inbuf;

				unblock_sigchld(&sigmask);
			}

			spc_state = SPC_Read_Data;

			/* XXX - This is rather a gross hack: if there are
			 * no data following the request, then we don't
			 * want to wait on data that's not going to come.
			 * By not continuing, we fall through directly to
			 * the next case, which will immediately send the
			 * request.
			 */
			if (spc_req.len > 0)
				continue;
		}

		/* State 1: Expecting to read SPC data. */
		if ((spc_state == SPC_Read_Data) &&
		    FD_ISSET(spcpipe[1], &in_fds))
		{
			/* Need to read (continue reading) SPC data */

			if (spc_toread > 0)
			{
				/* Read the next chunk */
				err = read(spcpipe[1], spcp, spc_toread);

				if (err < 0)
				{
					Error(_("%s: Error reading SPC "
						"request."),
					      "run_conduit");
					Perror("read");
					/* XXX - What now? Abort or
					 * something.
					 */
				}

				spc_toread -= err;
			}

			if (spc_toread > 0)
				/* There's more left to read. Wait for it */
				continue;

			/* Now we've read all of the SPC request, and can
			 * process it.
			 */
			block_sigchld(&sigmask);
			err = spc_send(&spc_req,
				       palm_pconn(palm),
				       dbinfo,
				       spc_inbuf,
				       (unsigned char **) &spc_outbuf);
				/* For error-checking, see below */
				/* NB: The cast of '&spc_outbuf' is utterly
				 * bogus, but is required to shut the
				 * compiler up. The compiler is required to
				 * complain by a technicality in the ANSI C
				 * spec.
				 * For more details, see Peter van der
				 * Linden, "Expert C Programming: Deep C
				 * Secrets", Prentice-Hall, 1994, Chap. 1,
				 * "Reading the ANSI C Standard for Fun,
				 * Pleasure, and Profit"
				 */
			unblock_sigchld(&sigmask);

			/* We're done with spc_inbuf */
			if (spc_inbuf != NULL)
			{
				free(spc_inbuf);
				spc_inbuf = NULL; /* Don't free it again. */
			}
			spc_towrite = spc_req.len;

			/* Error-checking */
			if (err < 0)
			{
				switch (cs_errno)
				{
				    case CSE_NOCONN:
					/* Lost connection to Palm */
					return 402;
				    default:
					/* Unspecified error */
					return 401;
				}
			}

			spc_state = SPC_Write_Header;

			continue;
		}

		/* State 2: Want to write the header of an SPC response */
		if ((spc_state == SPC_Write_Header) &&
		    FD_ISSET(spcpipe[1], &out_fds))
		{
			/* Need to send the SPC response header */
			static unsigned char spc_header[SPC_HEADER_LEN];

			CONDUIT_TRACE(5)
				fprintf(stderr,
					"Sending SPC response OP == %d, "
					"status == %d, "
					"len == %ld\n",
					spc_req.op,
					spc_req.status,
					spc_req.len);

			/* Write the header to 'spc_header' */
			/* XXX - This is very crude. There ought to be a
			 * function to do it.
			 */
			*((unsigned short *) spc_header) = htons(spc_req.op);
			*((unsigned short *) (spc_header+2)) =
				htons(spc_req.status);
			*((unsigned long *) (spc_header+4)) =
				htonl(spc_req.len);

			err = write(spcpipe[1], spc_header, SPC_HEADER_LEN);
			if (err != SPC_HEADER_LEN)
			{
				Error(_("%s: error sending SPC response "
					"header."),
				      "run_conduit");
				if (err < 0)
					Perror("write");
				/* XXX - What now? Abort? */
			}

			spc_towrite = spc_req.len;
			spcp = spc_outbuf;

			spc_state = SPC_Write_Data;
			continue;
		}

		/* State 3: Want to write the data of an SPC response */
		if ((spc_state == SPC_Write_Data) &&
		    FD_ISSET(spcpipe[1], &out_fds))
		{
			/* Need to send (continue sending) SPC response
			 * data.
			 */

			if (spc_towrite > 0)
			{
				CONDUIT_TRACE(5)
					fprintf(stderr, "Sending SPC data\n");

				/* Send the next chunk */
				err = write(spcpipe[1], spcp, spc_towrite);

				if (err < 0)
				{
					Error(_("%s: Error sending SPC "
						"response data."),
					      "run_conduit");
					Perror("write");
					/* XXX - What now? Abort? */
				}

				spc_towrite -= err;
				spcp += err;
			}

			if (spc_towrite <= 0)
			{
				/* We're done sending the request */
				free(spc_outbuf);
				spc_state = SPC_Read_Header;
			}
 
			continue;
		}
	}

  abort:
	if (conduit_pid > 0)
	{
		/* Presumably, we're here because there was an internal
		 * error, but the conduit isn't dead. Kill it.
		 */
		CONDUIT_TRACE(5)
			fprintf(stderr, "Sending SIGTERM to %d\n", conduit_pid);
		kill(conduit_pid, SIGTERM);
				/* No error checking, at least for now,
				 * since there was an internal error, and
				 * we have bigger things to worry about
				 * than whether the conduit caught its
				 * SIGTERM.
				 */
	}

	/* The conduit has exited */

	/* See if there is anything pending on 'fromchild' */
	while (1)
	{
		err = poll_fd(fileno(fromchild), False);

		if (err < 0)
		{
			/* An error occurred. I don't think we care */
			CONDUIT_TRACE(5)
				Perror("select");
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

	CONDUIT_TRACE(6)
		fprintf(stderr, "Closing child's file descriptors.\n");
	if (tochild != NULL)
	{
		CONDUIT_TRACE(7)
			fprintf(stderr, "- Closing fd %d\n", fileno(tochild));
		fpurge(tochild);	/* Just drop whatever may be
					 * lingering in the file handle, in
					 * case fclose() tries to write it
					 * to the now-dead child.
					 */
		fclose(tochild);
	}
	if (fromchild != NULL)
	{
		CONDUIT_TRACE(7)
			fprintf(stderr, "- Closing fd %d\n",
				fileno(fromchild));
		fpurge(fromchild);	/* Just drop whatever may be
					 * lingering in the file handle.
					 */
		fclose(fromchild);
	}

	if (with_spc)
	{
		/* A Palm doesn't allow all that many open databases
		 * at once. If a conduit terminates with a database open
		 * (prematurely or otherwise) it can lock out any other conduits
		 * that do DlpOpenDB calls. Since this includes essentially _all_
		 * sync conduits, we want to prevent it by just closing all the
		 * databases.
		 */

		DlpCloseDB(palm_pconn(palm), DLPCMD_CLOSEALLDBS, 0);

		close(spcpipe[0]);
		close(spcpipe[1]);
  	}

	/* Let's not hog memory */
	if (pref_list != NULL)
		free(pref_list);

	free_headers (&headers, &num_headers);

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
run_conduits(struct Palm *palm,
		const struct dlp_dbinfo *dbinfo,
		char *flavor,		/* Dump flavor: will be sent to
					 * conduit.
					 */
		unsigned short flavor_mask,
		const Bool with_spc,	/* Allow SPC calls? */
		pda_block *pda)
{
	int err;
	conduit_block *conduit;
	conduit_block *def_conduit;	/* Default conduit */
	Bool found_conduit;		/* Set to true if a "real" (not
					 * just a default) matching conduit
					 * was found.
					 */
	struct ConduitDef *builtin;

	udword creator, type;		/* DB creator, type and flags */
	unsigned char flags;



	def_conduit = NULL;		/* No default conduit yet */
	found_conduit = False;


	/* If dbinfo == NULL, we must execute "type: none" conduits. */ 
	if (dbinfo)
	{
		creator = dbinfo->creator;
		type = dbinfo->type;
		flags = 0L;
	}
	else
	{
		creator = type = 0L;
		flags = CREATYPEFL_ISNONE;
	}
	

	/* Walk the queue */
	for (conduit = sync_config->conduits;
	     conduit != NULL;
	     conduit = conduit->next)
	{
		CONDUIT_TRACE(3)
			fprintf(stderr, "Trying conduit %s...\n",
				(conduit->path == NULL ? "(null)" :
				 conduit->path));

		if (!conduit->enabled)
		{
			CONDUIT_TRACE(3)
				fprintf(stderr, "  Not enabled.\n");
			continue;
		}

		/* See if the flavor matches */
		if ((conduit->flavors & flavor_mask) == 0)
		{
			CONDUIT_TRACE(5)
				fprintf(stderr, "  Flavor set 0x%02x doesn't "
					"match %#x\n\t=>Not applicable.\n",
					conduit->flavors,
					flavor_mask);
			continue;
		}

		/* See if any of the creator/type pairs match */
		if (!crea_type_matches(conduit,
				       creator,
				       type,
				       flags))
		{
			CONDUIT_TRACE(5)
				fprintf(stderr,
					"  Creator/Type doesn't match\n"
					"\t=>Not applicable.\n");
			continue;
		}

		/* This conduit matches */
		CONDUIT_TRACE(2)
			fprintf(stderr, "  This conduit matches. "
				"Running \"%s\"\n",
				(conduit->path == NULL ? "(null)" :
				 conduit->path));

		if (conduit->flags & CONDFL_DEFAULT)
		{
			CONDUIT_TRACE(3)
				fprintf(stderr, "  This is a default conduit. "
					"Remembering for later.\n");

			/* Remember this conduit as the default if no other
			 * conduits match.
			 */
			def_conduit = conduit;
			continue;
		}

		found_conduit = True;

		/* See if it's a built-in conduit */
		if ((builtin = findConduitByName(conduit->path)) == NULL)
			/* It's an external program. Run it */
			err = run_conduit(palm, dbinfo, flavor, flavor_mask,
					  conduit, with_spc, pda);
		else {
			/* It's a built-in conduit. Run the appropriate
			 * function.
			 */

			/* Make sure the flavor is okay. */
			if ((flavor_mask & builtin->flavors) == 0)
			{
				Error(_("Conduit %s is not a %s conduit."),
				      builtin->name, flavor);
				continue;
			}

			err = (*builtin->func)(palm_pconn(palm), dbinfo, conduit, pda);
		}

		/* Error-checking */
		if (err < 0)
		{
			switch (cs_errno)
			{
			    case CSE_CANCEL:
			    case CSE_NOCONN:
				return -1;

			    default:
				Warn(_("Conduit %s exited abnormally. "
					  "Continuing."),
				     (conduit == NULL ||
				      conduit->path == NULL ? "(null)" :
				      conduit->path));
			}
		}

		/* If this is a final conduit, don't look any further. */
		if (conduit->flags & CONDFL_FINAL)
		{
			CONDUIT_TRACE(3)
				fprintf(stderr, "  This is a final conduit. "
					"Not looking any further.\n");

			return 0;
		}
	}

	if ((!found_conduit) && (def_conduit != NULL) && (dbinfo != NULL))
	{
		/* No matching conduit was found, but there's a default.
		 * Run it now. The default conduit is run only on "real"
		 * (i.e. no "none") databases.
		 */
		CONDUIT_TRACE(4)
			fprintf(stderr, "Running default conduit\n");

		/* See if it's a built-in conduit */
		if ((builtin = findConduitByName(def_conduit->path)) == NULL)
			/* It's an external program. Run it */
			err = run_conduit(palm, dbinfo, flavor, flavor_mask,
					  def_conduit, with_spc, pda);
		else {
			/* It's a built-in conduit. Run the appropriate
			 * function.
			 */

			/* Make sure the flavor is okay. */
			if ((flavor_mask & builtin->flavors) == 0)
			{
				Error(_("Conduit %s is not a %s conduit."),
				      builtin->name, flavor);
				return -1;
			}

			err = (*builtin->func)(palm_pconn(palm), dbinfo, def_conduit, pda);
		}

		/* Error-checking */
		if (err < 0)
		{
			switch (cs_errno)
			{
			    case CSE_CANCEL:
			    case CSE_NOCONN:
				return -1;

			    default:
				Warn(_("Conduit %s exited abnormally. "
					  "Continuing."),
				     (def_conduit == NULL ||
				      def_conduit->path == NULL ? "(null)" :
				      def_conduit->path));
			}
		}
	}

	return 0;
}

/* run_Fetch_conduits
 * Go through the list of Fetch conduits and run whichever ones are
 * applicable for the database 'dbinfo'.
 */
int
run_Fetch_conduits(struct Palm *palm, const struct dlp_dbinfo *dbinfo, pda_block *pda)
{
	CONDUIT_TRACE(1)
		fprintf(stderr, "Running pre-fetch conduits for \"%s\".\n",
			dbinfo != NULL ? dbinfo->name : "(none)");

	/* Note: If there are any open file descriptors that the conduit
	 * shouldn't have access to (other than stdin and stdout, which are
	 * handled separately, here would be a good place to close them.
	 * Use
	 *	fcntl(fd, F_SETFD, FD_CLOEXEC);
	 */

	return run_conduits(palm, dbinfo, "fetch", FLAVORFL_FETCH, False, pda);
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
run_Dump_conduits(struct Palm *palm, const struct dlp_dbinfo *dbinfo, pda_block *pda)
{
	CONDUIT_TRACE(1)
		fprintf(stderr, "Running post-dump conduits for \"%s\".\n",
			dbinfo != NULL ? dbinfo->name : "(none)");

	/* Note: If there are any open file descriptors that the conduit
	 * shouldn't have access to (other than stdin and stdout, which are
	 * handled separately, here would be a good place to close them.
	 * Use
	 *	fcntl(fd, F_SETFD, FD_CLOEXEC);
	 */

	return run_conduits(palm, dbinfo, "dump", FLAVORFL_DUMP, False, pda);
}

/* run_Sync_conduits
 * Go through the list of Sync conduits and run whichever ones are
 * applicable for the database 'dbinfo'.
 */
int
run_Sync_conduits(struct Palm *palm, const struct dlp_dbinfo *dbinfo,
		  pda_block *pda)
{
	CONDUIT_TRACE(1)
		fprintf(stderr, "Running sync conduits for \"%s\".\n",
			dbinfo != NULL ? dbinfo->name : "(none)");

	/* Note: If there are any open file descriptors that the conduit
	 * shouldn't have access to (other than stdin and stdout, which are
	 * handled separately, here would be a good place to close them.
	 * Use
	 *	fcntl(fd, F_SETFD, FD_CLOEXEC);
	 */

	return run_conduits(palm, dbinfo, "sync", FLAVORFL_SYNC, True, pda);
}

/* run_Init_conduits
 * Run the Init conduits.
 */
int
run_Init_conduits(struct Palm *palm)
{
	CONDUIT_TRACE(1)
		fprintf(stderr, "Running init conduits.\n");

	/* Note: If there are any open file descriptors that the conduit
	 * shouldn't have access to (other than stdin and stdout, which are
	 * handled separately, here would be a good place to close them.
	 * Use
	 *	fcntl(fd, F_SETFD, FD_CLOEXEC);
	 */

	return run_conduits(palm, NULL, "init", FLAVORFL_INIT, True, NULL);
}

/* run_Install_conduits 
 * Go through all Install conduits in the install directory and run
 * whichever ones are applicable for that database.
 */
int
run_Install_conduits(struct Palm *palm, struct dlp_dbinfo *dbinfo, pda_block *pda)
{
	CONDUIT_TRACE(1)
		fprintf(stderr, "Running install conduits for \"%s\".\n",
			dbinfo != NULL ? dbinfo->name : "(none)");

	return run_conduits(palm, dbinfo, "install", FLAVORFL_INSTALL, False, pda);
}

/* find_in_path
 * Looks for an executable conduit in the appropriate path. $CS_CONDUIT_PATH
 * is a colon-separated list of directories in which to look for conduits.
 * If a path component is empty, that means to look in "the usual places";
 * currently, this means $CS_CONDUITDIR, although in the future this might be
 * a system-wide path.
 *
 * If $CS_CONDUIT_PATH isn't set, find_in_path() tries "the usual places"
 * which, again, means $CS_CONDUITDIR.
 *
 * If none of these approaches work, return NULL, meaning that there is no
 * executable for this conduit.
 *
 * Returns a pathname that can be fed to execv(), or NULL if no executable
 * conduit can be found.
 */
static const char *
find_in_path(const char *conduit)
{
	const char *path;		/* $CS_CONDUIT_PATH */
	const char *conduitdir;		/* $CS_CONDUITDIR */
	const char *colon;		/* Pointer to path separator */
	static char buf[MAXPATHLEN];	/* Holds current pathname attempt */

	/* If the pathname contains a slash, then it's either absolute or
	 * relative, and should remain as-is.
	 */
	if (strchr(conduit, '/') != NULL)
	{
		CONDUIT_TRACE(4)
			fprintf(stderr, "find_in_path: returning [%s]\n",
				conduit);
		return conduit;
	}

	/* XXX Shouldn't we free path and conduitdir somewhere? */

	path = get_symbol("CS_CONDUIT_PATH");
	conduitdir = get_symbol("CS_CONDUITDIR");

	CONDUIT_TRACE(4)
	{
		fprintf(stderr, "find_in_path: $CS_CONDUIT_PATH == [%s]\n",
			path);
		fprintf(stderr, "find_in_path: $CS_CONDUITDIR == [%s]\n",
			conduitdir);
	}

	/* If $CS_CONDUIT_PATH is empty, try $CS_CONDUITDIR */
	if ((path == NULL) || (path[0] == '\0'))
	{
		CONDUIT_TRACE(4)
			fprintf(stderr, "find_in_path: no $CS_CONDUIT_PATH\n");
		/* Empty or unset $CS_CONDUIT_PATH. */
		if ((conduitdir == NULL) || (conduitdir[0] == '\0'))
		{
			/* Empty or unset $CS_CONDUITDIR. */
			CONDUIT_TRACE(4)
				fprintf(stderr, "find_in_path: no "
					"$CS_CONDUITDIR, either. Returning "
					"NULL\n");
			return NULL;
		}
		snprintf(buf, MAXPATHLEN, "%s/%s", conduitdir, conduit);
		if (access(buf, X_OK) == 0)
		{
			CONDUIT_TRACE(3)
				fprintf(stderr, "find_in_path: returning "
					"[%s]\n",
					buf);
			return buf;
		}

		/* No $CS_CONDUIT_PATH, and $CS_CONDUITDIR/conduit isn't
		 * executable. Give up. */
		CONDUIT_TRACE(3)
			fprintf(stderr, "find_in_path: returning NULL\n");
		return NULL;
	}

	/* Look in each path component in turn */
	while (path != NULL)
	{
		colon = strchr(path, ':');
		if (colon == NULL)
		{
			/* Last path component */
			if (strlen(path) == 0)
			{
				/* Empty final path component. Try
				 * $CS_CONDUITDIR/conduit
				 */
				if ((conduitdir != NULL) &&
				    (conduitdir[0] != '\0'))
				{
					snprintf(buf, MAXPATHLEN, "%s/%s",
						 conduitdir, conduit);
				}
			} else {
				/* Non-empty final path component */
				snprintf(buf, MAXPATHLEN, "%s/%s",
					 path, conduit);
			}
			path = NULL;
		} else {
			if (colon == path)
			{
				/* Empty path component. Try
				 * $CS_CONDUITDIR/conduit
				 */
				if ((conduitdir != NULL) &&
				    (conduitdir[0] != '\0'))
				{
					snprintf(buf, MAXPATHLEN, "%s/%s",
						 conduitdir, conduit);
				}
			} else {
				/* Non-empty path component */
				snprintf(buf, MAXPATHLEN, "%.*s/%s",
					 colon-path, path,
					 conduit);
			}
			path = colon + 1;
		}

		/* See if the pathname in 'buf' is executable */
		CONDUIT_TRACE(4)
			fprintf(stderr, "find_in_path: Trying [%s]\n", buf);
		if (access(buf, X_OK) == 0)
		{
			CONDUIT_TRACE(4)
				fprintf(stderr, "find_in_path: "
					"returning [%s]\n", buf);
			return buf;
		}
	}

	/* Nothing is executable. */
	CONDUIT_TRACE(4)
		fprintf(stderr, "find_in_path: returning NULL\n");
	return NULL;
}

static int
mychdir(const char *directory)
{
	CONDUIT_TRACE(4)
		fprintf(stderr, "Switching to directory: \"%s\"\n", directory);   

	if (chdir(directory) != 0)
	{
		Error(_("%s: Couldn't change directory to \"%s\"."),
		      "spawn_conduit", directory);

		return -1;
	}

	return 0;
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
	const char *cwd,		/* Conduit working directory */
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
	sigset_t sigmask;	/* Mask of signals to block. Used by
				 * {,un}block_sigchld() */
	const char *fname;	/* (Usually full) pathname to conduit */

	/* Set up the pipes for communication with the child */
	/* Child's stdin */
	if ((err = pipe(inpipe)) < 0)
	{
		Perror("pipe(inpipe)");
		return -1;
	}
	CONDUIT_TRACE(6)
		fprintf(stderr, "spawn_conduit: inpipe == %d, %d\n",
			inpipe[0], inpipe[1]);

	/* Turn this file descriptor into a file handle */
	CONDUIT_TRACE(5)
		fprintf(stderr, "spawn_conduit: tochild fd == %d\n",
			inpipe[1]);

	if ((fh = fdopen(inpipe[1], "wb")) == NULL)
	{
		Error(_("%s: Can't create file handle to child's stdin."),
		      "spawn_conduit");
		Perror("fdopen");

		close(inpipe[0]);
		close(inpipe[1]);
		return -1;
	}
	*tochild = fh;

	/* Child's stdout */
	if ((err = pipe(outpipe)) < 0)
	{
		Perror("pipe(outpipe)");

		close(inpipe[0]);
		close(inpipe[1]);
		return -1;
	}
	CONDUIT_TRACE(6)
		fprintf(stderr, "spawn_conduit: outpipe == %d, %d\n",
			outpipe[0], outpipe[1]);
	CONDUIT_TRACE(5)
		fprintf(stderr, "spawn_conduit: fromchild fd == %d\n",
			outpipe[0]);

	if ((fh = fdopen(outpipe[0], "rb")) == NULL)
	{
		Error(_("%s: Can't create file handle to child's stdout."),
		      "spawn_conduit");
		Perror("fdopen");

		close(inpipe[0]);
		close(inpipe[1]);
		close(outpipe[0]);
		close(outpipe[1]);
		return -1;
	}

	/* Make the child's stdout be line-buffered. This is because the
	 * child will be reporting status back on stdout, so we want to
	 * hear about it as soon as it happens.
	 */
	err = setvbuf(fh, cond_stdout_buf, _IOLBF, sizeof(cond_stdout_buf));
	if (err < 0)
	{
		Error(_("%s: Can't make child's stdout be line-buffered."),
		      "spawn_conduit");
		Perror("setvbuf");

		close(inpipe[0]);
		close(inpipe[1]);
		close(outpipe[0]);
		close(outpipe[1]);
		return -1;
	}
	*fromchild = fh;

	/* Here begins the critical section. The conduit might die
	 * immediately (e.g., the file doesn't exist, or isn't executable,
	 * or dumps core immediately). However, we don't want to get a
	 * SIGCHLD before spawn_conduit() has finished cleaning up from the
	 * fork(). Hence, we block SIGCHLD until we're done.
	 */
	block_sigchld(&sigmask);

	if ((conduit_pid = fork()) < 0)
	{
		Perror("fork");
		return -1;
	} else if (conduit_pid != 0)
	{
		/* This is the parent */
		CONDUIT_TRACE(5)
			fprintf(stderr, "Conduit PID == %d\n",
				(int) conduit_pid);

		/* Close the unused ends of the pipes */
		close(inpipe[0]);
		close(outpipe[1]);

		/* Here ends the critical section of the parent. Unblock
		 * SIGCHLD.
		 */
		unblock_sigchld(&sigmask);

		return conduit_pid;
	}

	/* This is the child */

	/* Close the unused ends of the pipes */
	close(inpipe[1]);
	close(outpipe[0]);

	/* We don't close stdin and stdout because dup2() already does so,
	 * but does it atomically.
	 */

	/* Dup stdin to the pipe. When dump conduits are run, the connection
	 * to the PDA has been closed. When running from inetd, that's STDIN
	 * and STDOUT, so we want to explicitly handle that case.
	 */
	if (inpipe[0] != STDIN_FILENO)
	{
		if ((err = dup2(inpipe[0], STDIN_FILENO)) < 0)
		{
			Perror("dup2(stdin)");
			exit(1);
		}
		close(inpipe[0]);
	}

	/* Dup stdout to the pipe */
	if (outpipe[1] != STDOUT_FILENO)
	{
		if ((err = dup2(outpipe[1], STDOUT_FILENO)) < 0)
		{
			Perror("dup2(stdout)");
			exit(1);
		}
		close(outpipe[1]);
	}

	/* Unblock SIGCHLD in the child as well. */
	unblock_sigchld(&sigmask);

	/* Find pathname to executable */
	fname = find_in_path(path);
	if (fname == NULL)
	{
		Error(_("%s: No executable \"%s\" in $CONDUIT_PATH or "
			"$CS_CONDUITDIR.\n"),
		      "spawn_conduit", path);
		exit(1);
	}
	
	if (cwd != NULL)
	{
		CONDUIT_TRACE(4)
			fprintf(stderr, "Obeying to cwd param: %s\n", cwd);	
	
		/* cwd to the directory in which the conduit resides */
		if (strcmp(cwd, "conduit") == 0)
		{
			char *dpath;
			
			if ((dpath = strdup(path)) != NULL) /* Preserve path since dirname will modify it */
			{
				char *newdir;
			
				if ((newdir = dirname(dpath)) != NULL)
					mychdir(newdir);	

				free(dpath);
			}			
		}
		/* cwd to the user's home directory */
		else if (strcmp(cwd, "home") == 0)
		{
			struct passwd *pw;

			uid_t uid = getuid();
			
			if ((pw = getpwuid(uid)) != NULL)
			{
				if (pw->pw_dir)
					mychdir(pw->pw_dir);
				else
					Error(_("%s: No home directory for %s."),
						"spawn_conduit", pw->pw_name);
			}
			else
			 	Error(_("%s: getpwuid(%d) failed."),
 	        	 	      "spawn_conduit", uid);
		}
		else
		/* cwd to cwd ;) */
			mychdir(cwd);
	}

	err = execv(fname, argv);
				/* Use execv(), not execvp(): don't look in
				 * $PATH. We've already checked. */

	/* If we ever get to this point, then something went wrong */
	Error(_("%s: execv(%s) failed and returned %d."),
	      "spawn_conduit",
	      path, err);
	Perror("execv");
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

		CONDUIT_TRACE(5)
			fprintf(stderr, "cond_readline: About to select()\n");

		err = select(fromchild_fd+1, &infds, NULL, NULL, NULL);

		CONDUIT_TRACE(5)
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
				Error(_("%s: select() returned an "
					"unexpected error. This should "
					"never happen."),
				      "cond_readline");
				Perror("select");
				return -1;
			}

			/* select() was interrupted */
			if (conduit_pid > 0)
			{
				CONDUIT_TRACE(5)
					fprintf(stderr,
						"cond_readline: select() just "
						"got spooked, is all.\n");
				continue;
			}

			/* Poll the child to see if it printed something
			 * before exiting.
			 */
			/* XXX - Use poll_fd() */
			zerotime.tv_sec = 0;
			zerotime.tv_usec = 0;
			FD_ZERO(&infds);
			FD_SET(fromchild_fd, &infds);

			CONDUIT_TRACE(6)
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
			Error(_("%s: select() returned 0. This should never "
				"happen. Please notify the maintainer."),
			      "cond_sendline");
			return -1;
		}

		/* See if the child has printed something. */
		if (FD_ISSET(fromchild_fd, &infds))
		{
			char *s;
			int s_len;	/* Length of string read */

			CONDUIT_TRACE(6)
				fprintf(stderr,
					"Child has written something\n");

			s = fgets(buf, len, fromchild);
			if (s == NULL)
			{
				CONDUIT_TRACE(6)
					fprintf(stderr, "cond_readline: "
						"fgets() returned NULL\n");

				/* Error or end of file */
				if (feof(fromchild))
				{
					CONDUIT_TRACE(6)
						fprintf(stderr,
							"cond_readline: "
							"End of file\n");

					/* End of file */
					return 0;
				}

				/* File error */
				CONDUIT_TRACE(4)
					fprintf(stderr, "cond_readline: "
						"error in fgets()\n");
				Perror("cond_readline: fgets");

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
		Error(_("%s: Conduit is running happily, but hasn't printed "
			"anything.\n"
			"And yet, I was notified. This is a bug. Please "
			"notify the maintainer."),
		      "cond_readline");
	}

  abort:
	/* The conduit is dead. This is bad. */
	Error(_("%s: Conduit exited unexpectedly."),
	      "cond_sendline");

	CONDUIT_TRACE(2)
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
			fprintf(stderr, "I have no idea how this happened\n");
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
		CONDUIT_TRACE(3)
			fprintf(stderr, "cond_readstatus: Child exited "
				"unexpectedly(?)\n");
		return -1;
	} else if (err == 0)
	{
		/* End of file, or child exited normally */
		CONDUIT_TRACE(3)
			fprintf(stderr, "cond_readstatus: Child exited "
				"normally\n");
		return 0;
	}

	/* Chop off the trailing \n, if any */
	msglen = strlen(buf);
		/* XXX - Is 'buf' guaranteed to be NUL-terminated? */
	if (buf[msglen-1] == '\n')
		buf[msglen-1] = '\0';

	CONDUIT_TRACE(5)
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
	fprintf(stderr, "%s\n", errmsg);
				/* XXX add conduit name here.  */

	return errcode; 
}

/* sigchld_handler
 * Handler for SIGCHLD signal. It records the state of a child as soon as
 * it changes; that way, we have up-to-date information in other parts of
 * the code, and avoid nasty timing bugs.
 *
 * NB: When modifying this function, make sure that it contains only
 * reentrant functions (Stevens, AUP, 10.6)
 */
/* XXX - I think there are OSes that reset the signal handler to its
 * default value once a signal has been received. Find out about this, and
 * cope with it.
 */
static RETSIGTYPE
sigchld_handler(int sig)
{
	pid_t p;	/* Temporary variable. Return value from waitpid() */
	int old_errno;

	/* Save the old value of 'errno', in case this signal interrupted
	 * something that uses it.
	 */
	old_errno = errno;

	MISC_TRACE(4)
		fprintf(stderr, "Got a SIGCHLD\n");

	if (canjump != 1)
	{
		/* Unexpected signal. Ignore it */
		CONDUIT_TRACE(5)
			fprintf(stderr, "Unexpected signal. Ignoring.\n");
		errno = old_errno;	/* Restore old errno */
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
				"Ignoring.\n", (int) conduit_pid);
		errno = old_errno;	/* Restore old errno */
		return;
	}

	/* Find out what just happened */
	/* WUNTRACED not specified. This means we won't find out about
	 * stopped processes.
	 */
	p = waitpid(conduit_pid, &conduit_status, WNOHANG);
	if (p < 0)
	{
		Error(_("%s: Can't get child pid %d status."),
		      "sigchld_handler", conduit_pid );
		Perror("waitpid");

		conduit_pid = -1;

		CONDUIT_TRACE(4)
			fprintf(stderr, "siglongjmp(1)ing out of SIGCHLD.\n");
		errno = old_errno;	/* Restore old errno */
		siglongjmp(chld_jmpbuf, 1);
	}

	/* Find out whether the conduit process is still running */
	/* If WUNTRACED is ever deemed to be a good thing, we'll need to
	 * check WIFSTOPPED here as well.
	 */
	/* The check for p is there because p==0 is a valid return code when
	 * WNOHANG is used. p==0 usually indicates a spurious SIGCHLD of some
	 * sort. */
	if (p > 0 && (WIFEXITED(conduit_status) || WIFSIGNALED(conduit_status)))
	{
		MISC_TRACE(4)
			fprintf(stderr, "Conduit pid %d is no longer running.\n", p );
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

		CONDUIT_TRACE(4)
			fprintf(stderr, "siglongjmp(1)ing out of SIGCHLD.\n");
		errno = old_errno;	/* Restore old errno */
		siglongjmp(chld_jmpbuf, 1);
	}
	MISC_TRACE(5)
		fprintf(stderr, "Conduit pid %d is still running.\n", p);

	errno = old_errno;	/* Restore old errno */
	return;			/* Nothing to do */
}

/* crea_type_matches
 * Given a conduit_block, a conduit and a type, crea_type_matches() returns
 * True iff at least one entry in the array of acceptable creator/type
 * pairs in the conduit_block matches the given creator and type.
 */
static INLINE Bool
crea_type_matches(const conduit_block *cond,
		  const udword creator,
		  const udword type,
		  const unsigned char flags)
{
	int i;
	Bool retval;

	retval = False;			/* Start by assuming that it
					 * doesn't match.
					 */

	CONDUIT_TRACE(7)
		fprintf(stderr, "crea_type_matches: "
			"conduit \"%s\",\n"
			"\tcreator: [%c%c%c%c] (0x%08lx) / "
			"type: [%c%c%c%c] (0x%08lx)\n"
			"\tflags: 0x%02x\n",

			cond->path,

			(char) ((creator >>24) & 0xff),
			(char) ((creator >>16) & 0xff),
			(char) ((creator >> 8) & 0xff),
			(char) (creator & 0xff),
			creator,

			(char) ((type >>24) & 0xff),
			(char) ((type >>16) & 0xff),
			(char) ((type >> 8) & 0xff),
			(char) (type & 0xff),
			type,
			flags);

	/* Iterate over the array of creator/type pairs defined in the
	 * conduit_block.
	 */
	for (i = 0; i < cond->num_ctypes; i++)
	{
		/* Both the creator and the type need to match. However,
		 * either one may be a wildcard (0), which matches
		 * anything.
		 */
		CONDUIT_TRACE(7)
			fprintf(stderr, "crea_type_matches: Comparing "
				"[%c%c%c%c/%c%c%c%c] (0x%08lx/0x%08lx) (flags: %02x)\n",

				(char) ((cond->ctypes[i].creator >>24) & 0xff),
				(char) ((cond->ctypes[i].creator >>16) & 0xff),
				(char) ((cond->ctypes[i].creator >> 8) & 0xff),
				(char) (cond->ctypes[i].creator & 0xff),

				(char) ((cond->ctypes[i].type >>24) & 0xff),
				(char) ((cond->ctypes[i].type >>16) & 0xff),
				(char) ((cond->ctypes[i].type >> 8) & 0xff),
				(char) (cond->ctypes[i].type & 0xff),

				cond->ctypes[i].creator,
				cond->ctypes[i].type,
				cond->ctypes[i].flags);

		if(cond->ctypes[i].flags != flags)
			continue;

		if (((cond->ctypes[i].creator == creator) ||
		     (cond->ctypes[i].creator == 0L)) &&
		    ((cond->ctypes[i].type == type) ||
		     (cond->ctypes[i].type == 0L)))
		{
			CONDUIT_TRACE(7)
				fprintf(stderr, "crea_type_matches: "
					"Found a match.\n");
			return True;
		}
	}

	CONDUIT_TRACE(7)
		fprintf(stderr, "crea_type_matches: No match found.\n");
	return False;
}

struct ConduitDef *
findConduitByName(const char *name)
{
	int i;

	CONDUIT_TRACE(5)
		fprintf(stderr, "Looking for built-in conduit \"%s\"\n",
			(name == NULL ? "(null)" : name));

	/* Check for NULL path. */
	if (name == NULL)
		return NULL;

	for (i = 0; i < num_builtin_conduits; i++)
	{
		CONDUIT_TRACE(6)
			fprintf(stderr, "Comparing to \"%s\"\n",
				builtin_conduits[i].name);

		if (strcmp(name, builtin_conduits[i].name) == 0)
		{
			CONDUIT_TRACE(6)
				fprintf(stderr, "Found builtin conduit\n");

			/* Found it */
			return &(builtin_conduits[i]);
		}
	}

	return NULL;		/* Not found */
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
