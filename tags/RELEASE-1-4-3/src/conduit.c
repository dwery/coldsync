/* conduit.c
 *
 * Functions for dealing with conduits: looking them up, loading
 * libraries, invoking them, etc.
 *
 *	Copyright (C) 1999, 2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: conduit.c,v 2.13 2000-09-08 15:59:33 arensb Exp $
 */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>			/* For malloc(), free() */
#include <string.h>
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

#include <unistd.h>			/* For select(), write() */
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
#include "spc.h"
#include "pref.h"

typedef RETSIGTYPE (*sighandler) (int);	/* This is equivalent to FreeBSD's
					 * 'sig_t', but that's a BSDism.
					 */

static int run_conduits(struct dlp_dbinfo *dbinfo,
			char *flavor,
			unsigned short flavor_mask,
			const Bool with_spc,
			struct PConnection *pconn);
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
static INLINE Bool crea_type_matches(
	const conduit_block *cond,
	const udword creator,
	const udword type);

typedef int (*ConduitFunc)(struct PConnection *pconn,
			   struct dlp_dbinfo *dbinfo,
			   const conduit_block *block);

int run_DummyConduit(struct PConnection *pconn,
		     struct dlp_dbinfo *dbinfo,
		     const conduit_block *block);
extern int run_GenericConduit(
	struct PConnection *pconn,
	struct dlp_dbinfo *dbinfo,
	const conduit_block *block);
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
	  FLAVORFL_FETCH | FLAVORFL_DUMP | FLAVORFL_SYNC, },
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

	SYNC_TRACE(7)
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
	SYNC_TRACE(7)
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

/* format_header

 * A convenience function. Takes a buffer, a header name, and a header
 * value. Writes the header to 'buf', making sure that the result obeys the
 * rules for conduit headers:
 *	- Header name (the part before the colon) has no more than
 *	  COND_MAXHFIELDLEN characters.
 *	- The entire line, not counting the \n at the end or the
 *	  terminating NUL, is no more than COND_MAXLINELEN characters.
 */
static INLINE void
format_header(char *buf,
	      const char *name,
	      const char *value)
{
		strncpy(buf, name,  COND_MAXHFIELDLEN);
		strncat(buf, ": ",  COND_MAXLINELEN - strlen(buf));
		strncat(buf, value, COND_MAXLINELEN - strlen(buf));
		strncat(buf, "\n",  COND_MAXLINELEN - strlen(buf));

		/* Make sure it's terminated properly */
		buf[COND_MAXLINELEN] = '\n';
		buf[COND_MAXLINELEN] = '\0';
}

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
/* XXX - This function is rather ugly, since it tries to be everything to
 * all conduits. This, in turn, is because about half of what
 * run_conduit() does is common to all conduit flavors, so it is
 * appropriate that it be encapsulated in a function.
 * Is it possible to abstract out just the common parts, and let other
 * functions take care of the flavor types' idiosyncracies?
 */
#define MAX_SYS_HEADERS	10		/* Size of the array holding system
					 * headers */

static int
run_conduit(struct dlp_dbinfo *dbinfo,	/* The database to sync */
	    char *flavor,		/* Name of the flavor */
	    conduit_block *conduit,	/* Conduit to be run */
	    const Bool with_spc,	/* Allow SPC calls? */
	    struct PConnection *pconn)	/* Connection to Palm */
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
	struct cond_header headers[MAX_SYS_HEADERS];
				/* System headers */
	int last_header = 0;
	int spcpipe[2];		/* Pipe for SPC-based communication */
	char spcnumbuf[4];	/* Buffer to hold the child's SPC file
				 * descriptor number. This only goes up to
				 * 999, but if that's not enough, then
				 * there's a serious file descriptor leak
				 * going on.
				 */
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

	/* XXX - See if conduit->path is a predefined string (set up a
	 * table of built-in conduits). If so, run the corresponding
	 * built-in conduit.
	 */

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
		/* Set up a pair of pipes for talking SPC with the child */
		if ((err = socketpair(AF_UNIX, SOCK_STREAM, 0, spcpipe)) < 0)
		{
			perror("pipe(spcpipe)");
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

		SYNC_TRACE(6)
		{
			fprintf(stderr, "spcpipe == (%d, %d)\n",
				spcpipe[0], spcpipe[1]);
		}
	}

	/* Set handler for SIGCHLD, so that we can keep track of what
	 * happens to conduit child processes.
	 */
	old_sigchld = signal(SIGCHLD, sigchld_handler);
	if (old_sigchld == SIG_ERR)
	{
		fprintf(stderr, _("%s: Can't set signal handler\n"),
			"run_conduit");
		perror("signal");
		return -1;
	}

	/* Before all the jumping stuff, make sure the pref_list is
	 * allocated.
	 */
	SYNC_TRACE(6)
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
			"run_conduit");

		/* Let's hope that this isn't a fatal problem */
		goto abort;
	}

	/* Feed the various parameters to the child via 'tochild'. */

	/* Initialize the standard header values */
	block_sigchld(&sigmask);	/* Don't disturb me now */

	bakfname = mkbakfname(dbinfo);

	/* Turn the array of system headers into a linked list. */
	for (i = 0; i < MAX_SYS_HEADERS-1; i++)
		headers[i].next = &(headers[i+1]);
	headers[MAX_SYS_HEADERS-1].next = NULL;

	last_header = 0;
	headers[last_header].name = "Daemon";
	headers[last_header].value = PACKAGE;

	++last_header;
	headers[last_header].name = "Version";
	headers[last_header].value = VERSION;

	++last_header;
	headers[last_header].name = "InputDB";
	headers[last_header].value = (char *) bakfname;
				/* The cast is just to stop the compiler
				 * from complaining. */

	++last_header;
	headers[last_header].name = "OutputDB";
	headers[last_header].value = (char *) bakfname;
				/* The cast is just to stop the compiler
				 * from complaining. */

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

		SYNC_TRACE(4)
			fprintf(stderr,
				"run_conduit: sending preference %d: "
				"0x%08lx/%d\n",
				i,
				conduit->prefs[i].creator,
				conduit->prefs[i].id);

		pref_list[i] = GetPrefItem(&(conduit->prefs[i]));

		/* Set the header */
		++last_header;
		headers[last_header].name = "Preference";

		/* Build the preference line with sprintf() because
		 * snprintf() isn't portable.
		 */
		sprintf(tmpvalue, "%c%c%c%c/%d/%d\n",
			(char) (conduit->prefs[i].creator >> 24) & 0xff,
			(char) (conduit->prefs[i].creator >> 16) & 0xff,
			(char) (conduit->prefs[i].creator >> 8) & 0xff,
			(char) conduit->prefs[i].creator & 0xff,
			conduit->prefs[i].id,
			pref_list[i]->contents_info->len);
		headers[last_header].value = tmpvalue;
	}

	/* If the conduit might understand SPC, tell it what file
	 * descriptor to use to talk to the parent.
	 */
	if (with_spc)
	{
		++last_header;
		headers[last_header].name = "SPCPipe";
		/* XXX - Should barf if spcpipe[0] > 999 */
		sprintf(spcnumbuf, "%d", spcpipe[0]);
		headers[last_header].value = spcnumbuf;
	}

	/* Now append the user-supplied headers to the system headers. This
	 * is a semi-ugly hack that allows us to treat the whole set of
	 * headers as a single list.
	 */
	headers[last_header].next = conduit->headers;

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

		/* Create the header line */
		format_header(buf, hdr->name, hdr->value);
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
			SYNC_TRACE(5)
				perror("poll_fd");
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
		SYNC_TRACE(4)
			fprintf(stderr, ">>> %s: %s\n",
				hdr->name,
				hdr->value);

		while (len > 0)
		{
			SYNC_TRACE(7)
				fprintf(stderr, "writing chunk [%s] (%d)\n",
					bufp, len);
			err = write(fileno(tochild), bufp, len);
			if (err < 0)
			{
				/* An error occurred */
				fprintf(stderr, _("Error while sending "
						  "header to conduit.\n"));
				perror("write");
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
		fwrite(pref_list[i]->contents,
			1,
			pref_list[i]->contents_info->len,
			tochild);
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
				/* XXX - Should complain: this can't happen */
				break;
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
		SYNC_TRACE(7)
			fprintf(stderr,
				"run_conduit: select() returned %d\n",
				err);

		if (err < 0)
		{
			if (errno != EINTR)
			{
				fprintf(stderr,
					_("%s: error in select()\n"),
					"run_conduit");
				perror("select");

				/* EINTR is harmless. All of the other ways
				 * select() can return -1 are severe
				 * errors. I don't know how to continue.
				 */
				goto abort;
			}
		}
		if (err == 0)
			/* This should never happen */
			/* XXX - Should probably print an error message to
			 * this effect.
			 */
			continue;

		/* Check fromchild, to see if the child has printed a
		 * status message to stdout.
		 */
		if (FD_ISSET(fileno(fromchild), &in_fds))
		{
			SYNC_TRACE(4)
				fprintf(stderr,
					"Child has printed to stdout.\n");

			block_sigchld(&sigmask);
			err = cond_readstatus(fromchild);
			unblock_sigchld(&sigmask);

			SYNC_TRACE(2)
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

		if (!with_spc)
			/* From here on in, everything has to do with SPC */
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
				fprintf(stderr,
					_("%s: error reading SPC request "
					  "from conduit.\n"),
					"run_conduit");
				perror("read");
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
			}

			/* Very crude parsing of the received header */
			/* XXX - There should probably be a function to
			 * parse this.
			 */
			spc_req.op = ntohs(*((unsigned short *) spc_header));
			spc_req.len = ntohl(
				* ((unsigned long *) (spc_header+4)));

			SYNC_TRACE(5)
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
					fprintf(stderr,
						_("%s: Error reading SPC "
						  "request.\n"),
						"run_conduit");
					perror("read");
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
				       pconn,
				       dbinfo,
				       spc_inbuf,
				       (unsigned char **) &spc_outbuf);
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
				free(spc_inbuf);
			spc_towrite = spc_req.len;

			spc_state = SPC_Write_Header;

			continue;
		}

		/* State 2: Want to write the header of an SPC response */
		if ((spc_state == SPC_Write_Header) &&
		    FD_ISSET(spcpipe[1], &out_fds))
		{
			/* Need to send the SPC response header */
			static unsigned char spc_header[SPC_HEADER_LEN];

			SYNC_TRACE(5)
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
				fprintf(stderr,
					_("%s: error sending SPC response "
					  "header.\n"),
					"run_conduit");
				if (err < 0)
					perror("write");
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
				SYNC_TRACE(6)
					fprintf(stderr, "Sending SPC data\n");

				/* Send the next chunk */
				err = write(spcpipe[1], spcp, spc_towrite);

				if (err < 0)
				{
					fprintf(stderr,
						_("%s: Error sending SPC "
						  "response data.\n"),
						"run_conduit");
					perror("write");
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
	/* The conduit has exited */

	/* XXX - We may have jumped here because of an internal error, and
	 * not because the child has died. It would be good to kill the
	 * child if it isn't dead yet.
	 */

	/* See if there is anything pending on 'fromchild' */
	while (1)
	{
		err = poll_fd(fileno(fromchild), False);

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
		fclose(tochild);
	}
	if (fromchild != NULL)
	{
		SYNC_TRACE(7)
			fprintf(stderr, "- Closing fd %d\n",
				fileno(fromchild));
		fclose(fromchild);
	}

	/* Let's not hog memory */
	/* XXX - This is an array of pointers, but the individual elements
	 * are not freed. This might leak memory: most of the elements are
	 * really pointers into pref_cache (which gets freed later on), but
	 * some of these elements might get initialized from
	 * DownloadPrefItem(), and I'm not convinced that they get freed
	 * correctly.
	 */
	if (pref_list != NULL)
		free(pref_list);

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
	     unsigned short flavor_mask,
	     const Bool with_spc,	/* Allow SPC calls? */
	     struct PConnection *pconn)	/* Connection to Palm */
{
	int err;
	conduit_block *conduit;
	conduit_block *def_conduit;	/* Default conduit */
	Bool found_conduit;		/* Set to true if a "real" (not
					 * just a default) matching conduit
					 * was found.
					 */
	struct ConduitDef *builtin;

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

		/* See if any of the creator/type pairs match */
		if (!crea_type_matches(conduit,
				       dbinfo->creator,
				       dbinfo->type))
		{
			SYNC_TRACE(5)
				fprintf(stderr,
					"  Creator/Type doesn't match\n"
					"\t=>Not applicable.\n");
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

		/* See if it's a built-in conduit */
		if ((builtin = findConduitByName(conduit->path)) == NULL)
			/* It's an external program. Run it */
			err = run_conduit(dbinfo, flavor, conduit, with_spc,
					  pconn);
		else
			/* It's a built-in conduit. Run the appropriate
			 * function.
			 */
			err = (*builtin->func)(pconn, dbinfo, conduit);

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

		/* See if it's a built-in conduit */
		if ((builtin = findConduitByName(def_conduit->path)) == NULL)
			/* It's an external program. Run it */
			err = run_conduit(dbinfo, flavor, def_conduit,
					  with_spc, pconn);
		else
			/* It's a built-in conduit. Run the appropriate
			 * function.
			 */
			(*builtin->func)(pconn, dbinfo, def_conduit);
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

	return run_conduits(dbinfo, "fetch", FLAVORFL_FETCH, False, NULL);
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

	return run_conduits(dbinfo, "dump", FLAVORFL_DUMP, False, NULL);
}

/* run_Sync_conduits
 * Go through the list of Sync conduits and run whichever ones are
 * applicable for the database 'dbinfo'.
 *
 * XXX - This is just an experimental first draft so far
 */
int
run_Sync_conduits(struct dlp_dbinfo *dbinfo,
		  struct PConnection *pconn)
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

	return run_conduits(dbinfo, "sync", FLAVORFL_SYNC, True, pconn);
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
	sigset_t sigmask;	/* Mask of signals to block. Used by
				 * {,un}block_sigchld() */

	/* Set up the pipes for communication with the child */
	/* Child's stdin */
	if ((err = pipe(inpipe)) < 0)
	{
		perror("pipe(inpipe)");
		exit(1);	/* XXX - Shouldn't die so violently */
	}
	SYNC_TRACE(6)
		fprintf(stderr, "spawn_conduit: inpipe == %d, %d\n",
			inpipe[0], inpipe[1]);

	/* Turn this file descriptor into a file handle */
	SYNC_TRACE(5)
		fprintf(stderr, "spawn_conduit: tochild fd == %d\n",
			inpipe[1]);

	if ((fh = fdopen(inpipe[1], "wb")) == NULL)
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
		exit(1);	/* XXX - Shouldn't die so violently */
	}
	SYNC_TRACE(6)
		fprintf(stderr, "spawn_conduit: outpipe == %d, %d\n",
			outpipe[0], outpipe[1]);
	SYNC_TRACE(5)
		fprintf(stderr, "spawn_conduit: fromchild fd == %d\n",
			outpipe[0]);

	if ((fh = fdopen(outpipe[0], "rb")) == NULL)
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
	block_sigchld(&sigmask);

	if ((conduit_pid = fork()) < 0)
	{
		perror("fork");
		return -1;
	} else if (conduit_pid != 0)
	{
		/* This is the parent */
		SYNC_TRACE(5)
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
	unblock_sigchld(&sigmask);

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
			/* XXX - Use poll_fd() */
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
/* XXX - Make sure this contains only reentrant functions (Stevens, AUP,
 * 10.6)
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
		SYNC_TRACE(5)
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
		fprintf(stderr, _("%s: Can't get child process status\n"),
			"sigchld_handler");
		perror("waitpid");

		conduit_pid = -1;

		SYNC_TRACE(4)
			fprintf(stderr, "siglongjmp(1)ing out of SIGCHLD.\n");
		errno = old_errno;	/* Restore old errno */
		siglongjmp(chld_jmpbuf, 1);
	}

	/* Find out whether the conduit process is still running */
	/* If WUNTRACED is ever deemed to be a good thing, we'll need to
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
		errno = old_errno;	/* Restore old errno */
		siglongjmp(chld_jmpbuf, 1);
	}
	MISC_TRACE(5)
		fprintf(stderr, "Conduit is still running.\n");

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
		  const udword type)
{
	int i;
	Bool retval;

	retval = False;			/* Start by assuming that it
					 * doesn't match.
					 */

	SYNC_TRACE(7)
		fprintf(stderr, "crea_type_matches: "
			"conduit \"%s\",\n"
			"\tcreator: [%c%c%c%c] (0x%08lx) / "
			"type: [%c%c%c%c] (0x%08lx)\n",

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
			type);

	/* Iterate over the array of creator/type pairs defined in the
	 * conduit_block.
	 */
	for (i = 0; i < cond->num_ctypes; i++)
	{
		/* Both the creator and the type need to match. However,
		 * either one may be a wildcard (0), which matches
		 * anything.
		 */
		SYNC_TRACE(7)
			fprintf(stderr, "crea_type_matches: Comparing "
				"[%c%c%c%c/%c%c%c%c] (0x%08lx/0x%08lx)\n",

				(char) ((cond->ctypes[i].creator >>24) & 0xff),
				(char) ((cond->ctypes[i].creator >>16) & 0xff),
				(char) ((cond->ctypes[i].creator >> 8) & 0xff),
				(char) (cond->ctypes[i].creator & 0xff),

				(char) ((cond->ctypes[i].type >>24) & 0xff),
				(char) ((cond->ctypes[i].type >>16) & 0xff),
				(char) ((cond->ctypes[i].type >> 8) & 0xff),
				(char) (cond->ctypes[i].type & 0xff),

				cond->ctypes[i].creator,
				cond->ctypes[i].type);

		if (((cond->ctypes[i].creator == creator) ||
		     (cond->ctypes[i].creator == 0L)) &&
		    ((cond->ctypes[i].type == type) ||
		     (cond->ctypes[i].type == 0L)))
		{
			SYNC_TRACE(7)
				fprintf(stderr, "crea_type_matches: "
					"Found a match.\n");
			return True;
		}
	}

	SYNC_TRACE(7)
		fprintf(stderr, "crea_type_matches: No match found.\n");
	return False;
}

/* XXX - Experimental */
int run_DummyConduit(struct PConnection *pconn,
		     struct dlp_dbinfo *dbinfo,
		     const conduit_block *block)
{
	fprintf(stderr, "Inside run_DummyConduit\n");
	return 0;
}

struct ConduitDef *
findConduitByName(const char *name)
{
	int i;

	for (i = 0; i < num_builtin_conduits; i++)
	{
		if (strcmp(name, builtin_conduits[i].name) == 0)
		{
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
