/* archive.c
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: archive.c,v 1.17 2000-11-24 22:55:37 arensb Exp $
 */

#include "config.h"
#include <stdio.h>

#if STDC_HEADERS
# include <string.h>		/* For strncat(), memcpy() et al. */
#else	/* STDC_HEADERS */
# ifndef HAVE_STRCHR
#  define strchr index
#  define strrchr rindex
# endif	/* HAVE_STRCHR */
# ifndef HAVE_MEMCPY
#  define memcpy(d,s,n)		bcopy ((s), (d), (n))
#  define memmove(d,s,n)	bcopy ((s), (d), (n))
# endif	/* HAVE_MEMCPY */
#endif	/* STDC_HEADERS */

#include <fcntl.h>		/* For open() */
#include <sys/param.h>		/* For MAXPATHLEN */
#include <sys/types.h>		/* For write() */
#include <sys/uio.h>		/* For write() */
#include <unistd.h>		/* For write(), lseek() */
#include <time.h>		/* For time() */
#include <errno.h>		/* For errno */

#if HAVE_LIBINTL_H
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL_H */

#include "pconn/pconn.h"
#include "coldsync.h"
#include "archive.h"

/* arch_create
 * Create a new archive file and initialize it with information from
 * 'dbinfo'. Opens the newly-created file and returns a file
 * descriptor for it, or -1 in case of error.
 */
int
arch_create(const struct dlp_dbinfo *dbinfo)
{
	int err;
	int fd;				/* File descriptor; will be returned */
	const char *archfname;		/* Name of the archive file */
	ubyte headerbuf[ARCH_HEADERLEN];	/* Archive header to write */
	ubyte *wptr;			/* Pointer into buffers, for writing */

	archfname = mkarchfname(dbinfo);
			/* Construct the name of the archive file */

	/* Open the file for writing; create it if it exists, truncate
	 * it otherwise.
	 * Create it with fascist permissions, since presumably
	 * this'll contain private information.
	 */
	if ((fd = open((const char *) archfname,
		       O_RDWR | O_CREAT | O_TRUNC | O_BINARY,
		       0600)) < 0)
	{
		fprintf(stderr, _("%s: Can't open file \"%s\"\n"),
			"arch_create",
			archfname);
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
		fprintf(stderr, _("%s: Can't write archive file header\n"),
			"arch_create");
		perror("write");
		close(fd);
		return -1;
	}

	return fd;		/* Success */
}

/* arch_open
 * Open the archive file for 'dbinfo'. This will be a file under
 * ~/.palm/archive, named after the database.
 * 'flags' are passed to open(2), and should therefore be O_RDONLY,
 * O_WRONLY, O_RDWR and friends.
 * Returns a file descriptor for the archive file, or a negative value
 * in case of error.
 */
int
arch_open(const struct dlp_dbinfo *dbinfo,
	  int flags)
{
	int err;
	int fd;				/* Return value: file descriptor */
	const char *archfname;		/* Name of the archive file */

	archfname = mkarchfname(dbinfo);
				/* Construct the name of the archive file */

	/* Open the file according to the mode given in 'flags'. The
	 * third, 'mode' flag, is just there for paranoia, in case the
	 * caller specified O_CREAT, so that open() doesn't read bogus
	 * values from the stack.
	 */
	if ((fd = open((const char *) archfname, flags, 0600)) < 0)
	{
		if (errno != ENOENT)
		{
			fprintf(stderr, _("%s: Can't open \"%s\"\n"),
				"arch_open",
				archfname);
			perror("open");
		}
		return -1;
	}

	/* Seek to the end of the file */
	if ((err = lseek(fd, 0L, SEEK_END)) < 0)
	{
		fprintf(stderr, _("%s: Can't seek to end of file\n"),
			"arch_open");
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

	SYNC_TRACE(6)
	{
		fprintf(stderr,
			"arch_writerecord: Archiving record, %ld bytes\n",
			rec->data_len);
		debug_dump(stderr, "ARCH", rec->data, rec->data_len);
	}

	write(fd, rec->data, rec->data_len);
	/* XXX - Error-checking */

	return 0;
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
