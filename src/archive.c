/* archive.c
 *
 * $Id: archive.c,v 1.3 1999-08-01 08:02:11 arensb Exp $
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>		/* For getenv() */
#include <string.h>		/* For strncat() et al. */
#include <fcntl.h>		/* For open() */
#include <sys/param.h>		/* For MAXPATHLEN */
#include <sys/types.h>		/* For write() */
#include <sys/uio.h>		/* For write() */
#include <unistd.h>		/* For write(), lseek() */
#include <time.h>		/* For time() */
#include "palm_types.h"
#include "util.h"		/* For put_*() */
#include "archive.h"

/* arch_create
 * Create a new archive file and initialize it with information from
 * 'dbinfo'. Opens the newly-created file and returns a file
 * descriptor for it, or -1 in case of error.
 */
int
arch_create(char *fname,
	    const struct dlp_dbinfo *dbinfo)
{
	int err;
	int fd;				/* File descriptor; will be returned */
	char fnamebuf[MAXPATHLEN];	/* Name of the archive file */
	ubyte headerbuf[ARCH_HEADERLEN];	/* Archive header to write */
	ubyte *wptr;			/* Pointer into buffers, for writing */

	/* Construct the name of the archive file */
	if (fname[0] == '/')
	{
		/* 'fname' is an absolute pathname, so just use that */
		strncpy(fnamebuf, fname, MAXPATHLEN-1);
		fnamebuf[MAXPATHLEN-1] = '\0';	/* Terminate the string */
	} else {
		/* 'fname' is a relative pathname; take it to be
		 * relative to ~/.palm/archive; construct that.
		 */
		/* XXX - Use 'archivedir' from coldsync.h */

		char *home;	/* User's home directory */
		int len;	/* Length of filename so far */

		if ((home = getenv("HOME")) == NULL)
		{
			fprintf(stderr, "arch_create: can't get $HOME\n");
			perror("getenv");
			return -1;
		}

		strncpy(fnamebuf, home, MAXPATHLEN-1);
		fnamebuf[MAXPATHLEN-1] = '\0';	/* Terminate the string */
		len = strlen(fnamebuf);

		/* Append "/.palm/archive/" to the pathname so far */
		strncat(fnamebuf, "/.palm/archive/", MAXPATHLEN-len-1);
		len = strlen(fnamebuf);

		/* Append 'fname' to the pathname so far */
		strncat(fnamebuf, fname, MAXPATHLEN-len-1);
	}

	/* Open the file for writing; create it if it exists, truncate
	 * it otherwise.
	 * Create it with fascist permissions, since presumably
	 * this'll contain private information.
	 */
	if ((fd = open(fnamebuf, O_RDWR | O_CREAT | O_TRUNC, 0600)) < 0)
	{
		fprintf(stderr, "arch_create: Can't open file \"%s\"\n",
			fnamebuf);
		perror("open");
		return -1;
	}

	/* Construct the archive header */
	wptr = headerbuf;
	memcpy(wptr, ARCH_MAGIC, ARCH_MAGIC_LEN);
	wptr += ARCH_MAGIC_LEN;
	put_uword(&wptr, ARCH_HEADERLEN);
	put_udword(&wptr, ARCH_FORMAT_VERSION);
	memcpy(wptr, dbinfo->name, PDB_DBNAMELEN);
	wptr += PDB_DBNAMELEN;
	put_udword(&wptr, dbinfo->type);
	put_udword(&wptr, dbinfo->creator);

	/* Write the archive header to the file */
	if ((err = write(fd, headerbuf, ARCH_HEADERLEN)) < 0)
	{
		fprintf(stderr, "arch_create: Can't write archive file header\n");
		perror("write");
		close(fd);
		return -1;
	}

	return fd;		/* Success */
}

/* arch_open
 * Open the archive file 'fname' (which may be relative or absolute)
 * either for reading, writing, or both, depending on the value of
 * 'flags' (which takes the same values as open(2)).
 * Returns a file descriptor for the archive file, or a negative value
 * in case of error.
 */
int
arch_open(char *fname,
	  int flags)
{
	int err;
	int fd;				/* Return value: file descriptor */
	char fnamebuf[MAXPATHLEN];	/* Name of the archive file */

	/* Construct the name of the archive file */
	if (fname[0] == '/')
	{
		/* 'fname' is an absolute pathname, so just use that */
		strncpy(fnamebuf, fname, MAXPATHLEN-1);
		fnamebuf[MAXPATHLEN-1] = '\0';	/* Terminate the string */
	} else {
		/* 'fname' is a relative pathname; take it to be
		 * relative to ~/.palm/archive; construct that.
		 */
		/* XXX - This should use 'archivedir' from coldsync.h */

		char *home;	/* User's home directory */
		int len;	/* Length of filename so far */

		if ((home = getenv("HOME")) == NULL)
		{
			fprintf(stderr, "arch_create: can't get $HOME\n");
			perror("getenv");
			return -1;
		}

		strncpy(fnamebuf, home, MAXPATHLEN-1);
		fnamebuf[MAXPATHLEN-1] = '\0';	/* Terminate the string */
		len = strlen(fnamebuf);

		/* Append "/.palm/archive/" to the pathname so far */
		strncat(fnamebuf, "/.palm/archive/", MAXPATHLEN-len-1);
		len = strlen(fnamebuf);

		/* Append 'fname' to the pathname so far */
		strncat(fnamebuf, fname, MAXPATHLEN-len-1);
	}

	/* Open the file according to the mode given in 'flags'. The
	 * third, 'mode' flag, is just there for paranoia, in case the
	 * caller specified O_CREAT, so that open() doesn't read bogus
	 * values from the stack.
	 */
	if ((fd = open(fnamebuf, flags, 0600)) < 0)
	{
		fprintf(stderr, "arch_open: Can't open \"%s\"\n", fnamebuf);
		perror("open");
		return -1;
	}

	/* Seek to the end of the file */
	if ((err = lseek(fd, 0L, SEEK_END)) < 0)
	{
		fprintf(stderr, "arch_open: Can't seek to end of file\n");
		perror("lseek");
		close(fd);
		return -1;
	}

	return fd;		/* Success */
}

/* arch_writerecord
 * Write 'rec' to the archive file whose (open) file descriptor is
 * 'fd'.
 * Returns 0 if successful, -1 otherwise.
 */
int
arch_writerecord(int fd,
		 const struct arch_record *rec)
{
	static ubyte headerbuf[ARCH_RECLEN];	/* Output buffer */
	ubyte *wptr;		/* Pointer into buffers, for writing */

	wptr = headerbuf;
	put_ubyte(&wptr, rec->type);
	put_ubyte(&wptr, ARCH_RECLEN);
	put_udword(&wptr, rec->data_len);
	put_udword(&wptr, time(NULL));
	write(fd, headerbuf, ARCH_RECLEN);
	/* XXX - Error-checking */

	write(fd, rec->data, rec->data_len);
	/* XXX - Error-checking */

	return 0;
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
