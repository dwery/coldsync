/* archive.h
 *
 * Definitions and structures for archive files.
 *
 * An archive file consists of a file header followed by zero or more
 * records. Each record consists of 
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: archive.h,v 1.6 2000-01-27 02:03:38 arensb Exp $
 */
#ifndef _archive_h_
#define _archive_h_

#include "config.h"
#include <fcntl.h>		/* For mode_t */
#include "pconn/pconn.h"
#include "pdb.h"

#define ARCH_MAGIC_LEN	8		/* Length of magic string */
#define ARCH_MAGIC	"ColdArch"	/* Magic string that goes at the
					 * beginning of an archive file */

#define ARCH_FORMAT_VERSION	1	/* The highest file format version
					 * that this code understands. */

/* arch_header
 * This header goes at the beginning of each archive file. The magic string
 * identifies this as an archive file. The version gives the file format,
 * for backward-compatibility.
 */
struct arch_header
{
	char magic[ARCH_MAGIC_LEN];	/* Magic string at beginning of file */
	uword header_len;		/* Length of this header */
	uword flags;			/* Flags (not used yet) */
	udword version;			/* File format version */

	char name[PDB_DBNAMELEN];	/* Name of the database */
	udword type;			/* Database type */
	udword creator;			/* Database creator */
};

#define ARCH_HEADERLEN		ARCH_MAGIC_LEN + 2 + 4 + \
				PDB_DBNAMELEN + 4 + 4
					/* Length of file header in the
					 * file */

/* arch_record
 * Archive file record.
 */
struct arch_record
{
	ubyte type;		/* Type of record */
	ubyte header_len;	/* Length of this header */
	udword data_len;	/* Length of data following the header */
	udword ctime;		/* Time when this record was added to the
				 * file, in seconds since Jan. 1, 1970
				 * (Unix epoch) */
	ubyte *data;		/* Record data */
};

#define ARCH_RECLEN		1+1+4+4
					/* Length of record header in the
					 * file (does not include the
					 * record data itself). */

/* Archive record types, for arch_record.type */
#define ARCHREC_REC		0	/* Plain data record */
#define ARCHREC_RSRC		1	/* Resource record */
					/* XXX - Not used */
#define ARCHREC_APPINFO		2	/* AppInfo block */
#define ARCHREC_SORTINFO	3	/* SortInfo block */
					/* XXX - Not used */

/* Function prototype */
extern int arch_create(char *fname, const struct dlp_dbinfo *dbinfo);
extern int arch_open(const struct dlp_dbinfo *dbinfo, int flags);
extern int arch_readheader(int fd, struct arch_header *header);	/* XXX */
extern int arch_readrecord(int fd, struct arch_record *rec);	/* XXX */
extern int arch_writerecord(int fd, const struct arch_record *rec);

#endif	/* _archive_h_ */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
