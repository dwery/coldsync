/* pdb.c
 *
 * Functions for dealing with Palm databases and such.
 *
 * $Id: pdb.c,v 1.4 1999-02-24 13:18:13 arensb Exp $
 */
#include <stdio.h>
#include <fcntl.h>		/* For open() */
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include "palm/palm_types.h"
#include "pconn/util.h"
#include "palm/pdb.h"

/* A macro to tell whether 'db' is a resource database or not. */
#define IS_RSRC_DB(db) 		((db)->header.attributes & PDB_ATTR_RESDB)

/* XXX - Need functions:
 *
 * pdb *read_pdb(fd) - Read a PDB/PRC from a file, return it as a data
 * structure
 *
 * int write_pdb(fd, struct pdb *pdb) - Write a struct pdb to a file.
 */
struct pdb *LoadDatabase(char *fname);
struct pdb *new_pdb();
static uword get_file_length(int fd);

static int pdb_LoadHeader(int fd, struct pdb *db);
static int pdb_LoadRecListHeader(int fd, struct pdb *db);
static int pdb_LoadRsrcIndex(int fd, struct pdb *db);
static int pdb_LoadRecIndex(int fd, struct pdb *db);
static int pdb_LoadAppBlock(int fd, struct pdb *db);
static int pdb_LoadSortBlock(int fd, struct pdb *db);
static int pdb_LoadResources(int fd, struct pdb *db);
static int pdb_LoadRecords(int fd, struct pdb *db);

/* XXX - Document the format of database files */
struct pdb *
LoadDatabase(char *fname)
{
	int err;
	struct pdb *retval;
	int fd;				/* File descriptor */
	static ubyte useless_buf[2];	/* Buffer for the useless 2 bytes
					 * after the record index.
					 */

	/* Open the file */
	if ((fd = open(fname, O_RDONLY)) < 0)
	{
		perror("LoadDatabase: open");
		return NULL;
	}

	/* Create a new pdb to return */
	if ((retval = new_pdb()) == NULL)
	{
		close(fd);
		return NULL;
	}

	/* Find out how long the file is */
	retval->file_size = get_file_length(fd);

	/* Load the header */
	if ((err = pdb_LoadHeader(fd, retval)) < 0)
	{
		fprintf(stderr, "Can't load header\n");
		free_pdb(retval);
		close(fd);
		return NULL;
	}

	/* Load the record list header */
	if ((err = pdb_LoadRecListHeader(fd, retval)) < 0)
	{
		fprintf(stderr, "Can't load header\n");
		free_pdb(retval);
		close(fd);
		return NULL;
	}

	/* Read the record/resource list */
	if (IS_RSRC_DB(retval))
	{
		/* Read the resource index */
		if ((err = pdb_LoadRsrcIndex(fd, retval)) < 0)
		{
			fprintf(stderr, "Can't read resource index\n");
			free_pdb(retval);
			close(fd);
			return NULL;
		}
	} else {
		/* Read the record index */
		if ((err = pdb_LoadRecIndex(fd, retval)) < 0)
		{
			fprintf(stderr, "Can't read record index\n");
			free_pdb(retval);
			close(fd);
			return NULL;
		}
	}

	/* Skip the dummy two bytes */
	if ((err = read(fd, useless_buf, 2)) != 2)
	{
		fprintf(stderr, "Can't read the useless two bytes");
		perror("LoadDatabase: read");
		free_pdb(retval);
		close(fd);
		return NULL;
	}
	/* Just a sanity check */
	if ((useless_buf[0] != '\0') ||
	    (useless_buf[1] != '\0'))
	{
		fprintf(stderr, "The useless two bytes contain 0x%02x%02x instead of NULs. This is unexpected.\n",
			useless_buf[0],
			useless_buf[1]);
	}

	/* Load the AppInfo block, if any */
	if ((err = pdb_LoadAppBlock(fd, retval)) < 0)
	{
		fprintf(stderr, "Can't read AppInfo block\n");
		free_pdb(retval);
		close(fd);
		return NULL;
	}

	/* Load the sort block, if any */
	if ((err = pdb_LoadSortBlock(fd, retval)) < 0)
	{
		fprintf(stderr, "Can't read sort block\n");
		free_pdb(retval);
		close(fd);
		return NULL;
	}

	/* Load the records themselves */
	if (IS_RSRC_DB(retval))
	{
		/* Read the resources */
		if ((err = pdb_LoadResources(fd, retval)) < 0)
		{
			fprintf(stderr, "Can't read resources.\n");
			free_pdb(retval);
			close(fd);
			return NULL;
		}
	} else {
		/* Read the records */
		if ((err = pdb_LoadRecords(fd, retval)) < 0)
		{
			fprintf(stderr, "Can't read records.\n");
			free_pdb(retval);
			close(fd);
			return NULL;
		}
	}

	free_pdb(retval);
	close(fd);

	return retval;			/* Success */
}

/* new_pdb
 * struct pdb constructor.
 */
struct pdb *
new_pdb()
{
	struct pdb *retval;

	/* Allocate the new pdb */
	if ((retval = (struct pdb *) malloc(sizeof(struct pdb))) == NULL)
		/* Out of memory */
		return NULL;

	return retval;
}

/* XXX - Need a function to write database files. Make sure it's paranoid,
 * doesn't overwrite existing files, does locking properly (even over NFS
 * (ugh!)).
 */

/* free_pdb
 * Cleanly free a struct pdb, and all of its subparts (destructor).
 */
void
free_pdb(struct pdb *db)
{
	if (db == NULL)
		/* Trivial case */
		return;

	/* Free the app info block */
	if (db->appinfo != NULL)
		free(db->appinfo);

	/* Free either the resource or record index, as appropriate */
	if (db->header.attributes & PDB_ATTR_RESDB)
	{
		if (db->rec_index.res != NULL)
			free(db->rec_index.res);
	} else {
		if (db->rec_index.rec != NULL)
			free(db->rec_index.rec);
	}

	free(db);
}

static uword
get_file_length(int fd)
{
	off_t here;
	off_t eof;

	/* Get the current position within the file */
	here = lseek(fd, 0, SEEK_CUR);
	/* XXX - What if the file isn't seekable? */

	/* Go to the end of the file */
	eof = lseek(fd, 0, SEEK_END);

	/* And return to where we were before */
	lseek(fd, here, SEEK_SET);

	return eof - here;
}

static int
pdb_LoadHeader(int fd,
	       struct pdb *db)
{
	int err;
	static ubyte buf[PDB_HEADER_LEN];
				/* Buffer to hold the file header */
	const ubyte *rptr;	/* Pointer into buffers, for reading */
time_t t;

	/* Read the header */
	if ((err = read(fd, buf, PDB_HEADER_LEN)) != PDB_HEADER_LEN)
	{
		perror("pdb_LoadHeader: read");
		return -1;
	}

	/* Parse the database header */
	rptr = buf;
	memcpy(db->header.name, buf, PDB_DBNAMELEN);
	rptr += PDB_DBNAMELEN;
	db->header.attributes = get_uword(&rptr);
	db->header.version = get_uword(&rptr);
	db->header.ctime = get_udword(&rptr);
	db->header.mtime = get_udword(&rptr);
	db->header.baktime = get_udword(&rptr);
	db->header.modnum = get_udword(&rptr);
	db->header.appinfoID = get_udword(&rptr);
	db->header.sortinfoID = get_udword(&rptr);
	db->header.type = get_udword(&rptr);
	db->header.creator = get_udword(&rptr);
	db->header.uniqueIDseed = get_udword(&rptr);

/* XXX - These printf() statements should be controlled by a
 * debugging flag.
 */
printf("\tname: \"%s\"\n", db->header.name);
printf("\tattributes: 0x%04x", db->header.attributes);
if (db->header.attributes & PDB_ATTR_RESDB) printf(" RESDB");
if (db->header.attributes & PDB_ATTR_RO) printf(" RO");
if (db->header.attributes & PDB_ATTR_APPINFODIRTY)
	 printf(" APPINFODIRTY");
if (db->header.attributes & PDB_ATTR_BACKUP) printf(" BACKUP");
if (db->header.attributes & PDB_ATTR_OKNEWER) printf(" OKNEWER");
if (db->header.attributes & PDB_ATTR_RESET) printf(" RESET");
if (db->header.attributes & PDB_ATTR_OPEN) printf(" OPEN");
printf("\n");
printf("\tversion: %u\n", db->header.version);
t = db->header.ctime - EPOCH_1904;
printf("\tctime: %lu %s", db->header.ctime,
	ctime(&t));
t = db->header.mtime - EPOCH_1904;
printf("\tmtime: %lu %s", db->header.mtime,
	ctime(&t));
t = db->header.baktime - EPOCH_1904;
printf("\tbaktime: %lu %s", db->header.baktime,
	ctime(&t));
printf("\tmodnum: %ld\n", db->header.modnum);
printf("\tappinfoID: 0x%08lx\n",
	db->header.appinfoID);
printf("\tsortinfoID: 0x%08lx\n",
	db->header.sortinfoID);
printf("\ttype: '%c%c%c%c' (0x%08lx)\n",
	(char) (db->header.type >> 24) & 0xff,
	(char) (db->header.type >> 16) & 0xff,
	(char) (db->header.type >> 8) & 0xff,
	(char) db->header.type & 0xff,
	db->header.type);
printf("\tcreator: '%c%c%c%c' (0x%08lx)\n",
	(char) (db->header.creator >> 24) & 0xff,
	(char) (db->header.creator >> 16) & 0xff,
	(char) (db->header.creator >> 8) & 0xff,
	(char) db->header.creator & 0xff,
	db->header.creator);
printf("\tuniqueIDseed: %ld\n", db->header.uniqueIDseed);

	return 0;		/* Success */
}

static int
pdb_LoadRecListHeader(int fd,
		      struct pdb *db)
{
	int err;
	static ubyte buf[PDB_RECORDLIST_LEN];
	const ubyte *rptr;	/* Pointer into buffers, for reading */

	/* Read the record list header */
	if ((err = read(fd, buf, PDB_RECORDLIST_LEN)) != PDB_RECORDLIST_LEN)
	{
		perror("pdb_LoadRecListHeader: read2");
		return -1;
	}

	/* Parse the record list */
	rptr = buf;
	db->reclist_header.nextID = get_udword(&rptr);
	db->reclist_header.len = get_uword(&rptr);

/* XXX - These printf() statements should be controlled by a
 * debugging flag.
 */
printf("\tnextID: %ld\n", db->reclist_header.nextID);
printf("\tlen: %u\n", db->reclist_header.len);

	return 0;
}

static int
pdb_LoadRsrcIndex(int fd,
		  struct pdb *db)
{
	int i;
	int err;
	static ubyte inbuf[PDB_RESOURCEIX_LEN];
					/* Input buffer */
	const ubyte *rptr;	/* Pointer into buffers, for reading */

	if (db->reclist_header.len == 0)
	{
		/* There are no resources in this file */
		db->rec_index.res = NULL;
		return 0;
	}

	/* It's a resource database. Allocate an array of resource index
	 * entries.
	 */
	if ((db->rec_index.res =
	     (struct pdb_resource *)
	     calloc(db->reclist_header.len,
		    sizeof(struct pdb_resource)))
	    == NULL)
	{
		fprintf(stderr, "Can't allocate resource list\n");
		return -1;
	}

	/* Read the resource index */
	for (i = 0; i < db->reclist_header.len; i++)
	{
		/* Read the next resource index entry */
		if ((err = read(fd, inbuf, PDB_RESOURCEIX_LEN)) !=
		    PDB_RESOURCEIX_LEN)
			return -1;

		/* Parse it */
		rptr = inbuf;
		db->rec_index.res[i].type = get_udword(&rptr);
		db->rec_index.res[i].id = get_uword(&rptr);
		db->rec_index.res[i].offset = get_udword(&rptr);

/* XXX - These printf() statements should be controlled by a
 * debugging flag.
 */
printf("\tResource %d: type '%c%c%c%c' (0x%08lx), id %u, offset 0x%04lx\n",
       i,
       (char) (db->rec_index.res[i].type >> 24) & 0xff,
       (char) (db->rec_index.res[i].type >> 16) & 0xff,
       (char) (db->rec_index.res[i].type >> 8) & 0xff,
       (char) db->rec_index.res[i].type & 0xff,
       db->rec_index.res[i].type,
       db->rec_index.res[i].id,
       db->rec_index.res[i].offset);
	}

	return 0;
}

static int
pdb_LoadRecIndex(int fd,
		 struct pdb *db)
{
	int i;
	int err;
	static ubyte inbuf[PDB_RECORDIX_LEN];
					/* Input buffer */
	const ubyte *rptr;	/* Pointer into buffers, for reading */

	if (db->reclist_header.len == 0)
	{
		/* There are no records in this file */
		db->rec_index.res = NULL;
		return 0;
	}

	/* It's a record database. Allocate an array of record index
	 * entries.
	 */
	if ((db->rec_index.rec =
	     (struct pdb_record *)
	     calloc(db->reclist_header.len,
		    sizeof(struct pdb_record)))
	    == NULL)
	{
		fprintf(stderr, "Can't allocate record list\n");
		return -1;
	}

	/* Read the record index */
	for (i = 0; i < db->reclist_header.len; i++)
	{
		/* Read the next record index entry */
		if ((err = read(fd, inbuf, PDB_RECORDIX_LEN)) !=
		    PDB_RECORDIX_LEN)
			return -1;

		/* Parse it */
		rptr = inbuf;
		db->rec_index.rec[i].offset = get_udword(&rptr);
		db->rec_index.rec[i].attributes = get_ubyte(&rptr);
		db->rec_index.rec[i].uniqueID[0] = get_ubyte(&rptr);
		db->rec_index.rec[i].uniqueID[1] = get_ubyte(&rptr);
		db->rec_index.rec[i].uniqueID[2] = get_ubyte(&rptr);

/* XXX - These printf() statements should be controlled by a
 * debugging flag.
 */
printf("\tRecord %d: offset 0x%04lx, attr 0x%02x, uniqueID 0x%02x%02x%02x\n",
       i,
       db->rec_index.rec[i].offset,
       db->rec_index.rec[i].attributes,
       db->rec_index.rec[i].uniqueID[0],
       db->rec_index.rec[i].uniqueID[1],
       db->rec_index.rec[i].uniqueID[2]);
	}

	return 0;
}

static int
pdb_LoadAppBlock(int fd,
		 struct pdb *db)
{
	int err;
	uword next_off;		/* Offset of the next thing in the file
				 * after the AppInfo block */
	off_t offset;		/* Offset into file, for checking */

	/* Check to see if there even *is* an AppInfo block */
	if (db->header.appinfoID == 0L)
	{
		/* Nope */
		db->appinfo_len = 0L;
		db->appinfo = NULL;
		return 0;
	}

	/* Figure out how long the AppInfo block is, by comparing its
	 * offset to that of the next thing in the file.
	 */
	if (db->header.sortinfoID > 0L)
		/* There's a sort block */
		next_off = db->header.sortinfoID;
	else if (db->reclist_header.len > 0)
	{
		/* There's no sort block, but there are records. Get the
		 * offset of the first one.
		 */
		if (IS_RSRC_DB(db))
			next_off = db->rec_index.res[0].offset;
		else
			next_off = db->rec_index.rec[0].offset;
	} else
		/* There is neither sort block nor records, so the AppInfo
		 * block must go to the end of the file.
		 */
		next_off = db->file_size;

	/* Subtract the AppInfo block's offset from that of the next thing
	 * in the file to get the AppInfo block's length.
	 */
	db->appinfo_len = next_off - db->header.appinfoID;

	/* This is probably paranoid, but what the hell */
	if (db->appinfo_len == 0L)
	{
		/* An effective no-op */
		db->appinfo = NULL;
		return 0;
	}

	/* Now that we know the length of the AppInfo block, allocate space
	 * for it and read it.
	 */
	if ((db->appinfo = (ubyte *) malloc(db->appinfo_len)) == NULL)
	{
		fprintf(stderr, "Out of memory\n");
		return -1;
	}

	/* Just out of paranoia, make sure we're at the correct offset in
	 * the file.
	 */
	offset = lseek(fd, 0, SEEK_CUR);	/* Find out where we are */
	if (offset != db->header.appinfoID)
	{
		/* Oops! We're in the wrong place */
		fprintf(stderr, "Warning: AppInfo block isn't where I thought it would be.\n"
			"expected 0x%lx, but we're at 0x%qx\n",
			db->header.appinfoID, offset);

		/* Try to recover */
		offset = lseek(fd, db->header.appinfoID, SEEK_SET);
					/* Go to where the AppInfo block
					 * ought to be */
		if (offset < 0)
		{
			/* Something's wrong */
			fprintf(stderr, "Can't find the AppInfo block!\n");
			return -1;
		}
	}

	/* Read the AppInfo block */
	if ((err = read(fd, db->appinfo, db->appinfo_len)) != db->appinfo_len)
	{
		perror("pdb_LoadAppBlock: read");
		return -1;
	}
/*  debug_dump(stdout, "<APP", db->appinfo, db->appinfo_len); */

	return 0; 
}

/* XXX - Largely untested, since not that many databases have sort blocks.
 * But it's basically just a clone of pdb_LoadAppBlock(), so it should be
 * okay.
 */
static int
pdb_LoadSortBlock(int fd,
		 struct pdb *db)
{
	int err;
	uword next_off;		/* Offset of the next thing in the file
				 * after the sort block */
	off_t offset;		/* Offset into file, for checking */

	/* Check to see if there even *is* a sort block */
	if (db->header.sortinfoID == 0L)
	{
		/* Nope */
		db->sortinfo_len = 0L;
		db->sortinfo = NULL;
		return 0;
	}

	/* Figure out how long the sort block is, by comparing its
	 * offset to that of the next thing in the file.
	 */
	if (db->reclist_header.len > 0)
	{
		/* There are records. Get the offset of the first one.
		 */
		if (IS_RSRC_DB(db))
			next_off = db->rec_index.res[0].offset;
		else
			next_off = db->rec_index.rec[0].offset;
	} else
		/* There are no records, so the sort block must go to the
		 * end of the file.
		 */
		next_off = db->file_size;

	/* Subtract the sort block's offset from that of the next thing
	 * in the file to get the sort block's length.
	 */
	db->sortinfo_len = next_off - db->header.sortinfoID;

	/* This is probably paranoid, but what the hell */
	if (db->sortinfo_len == 0L)
	{
		/* An effective no-op */
		db->sortinfo = NULL;
		return 0;
	}

	/* Now that we know the length of the sort block, allocate space
	 * for it and read it.
	 */
	if ((db->sortinfo = (ubyte *) malloc(db->sortinfo_len)) == NULL)
	{
		fprintf(stderr, "Out of memory\n");
		return -1;
	}

	/* Just out of paranoia, make sure we're at the correct offset in
	 * the file.
	 */
	offset = lseek(fd, 0, SEEK_CUR);	/* Find out where we are */
	if (offset != db->header.sortinfoID)
	{
		/* Oops! We're in the wrong place */
		fprintf(stderr, "Warning: sort block isn't where I thought it would be.\n"
			"Expected 0x%lx, but we're at 0x%qx\n",
			db->header.sortinfoID, offset);

		/* Try to recover */
		offset = lseek(fd, db->header.sortinfoID, SEEK_SET);
					/* Go to where the sort block
					 * ought to be */
		if (offset < 0)
		{
			/* Something's wrong */
			fprintf(stderr, "Can't find the sort block!\n");
			return -1;
		}
	}

	/* Read the sort block */
	if ((err = read(fd, db->sortinfo, db->sortinfo_len)) !=
	    db->sortinfo_len)
	{
		perror("pdb_LoadSortBlock: read");
		return -1;
	}
/*  debug_dump(stdout, "<SORT", db->sortinfo, db->sortinfo_len); */

	return 0; 
}

static int
pdb_LoadResources(int fd,
		  struct pdb *db)
{
	int i;
	int err;

	/* Allocate an array to hold the resources' sizes */
	if ((db->data_len = (uword *) calloc(db->reclist_header.len,
					     sizeof(uword))) == NULL)
	{
		fprintf(stderr, "Out of memory.\n");
		perror("pdb_LoadResources: malloc");
		return -1;
	}

	/* Allocate an array to hold the resources */
	if ((db->data = (ubyte **) calloc(db->reclist_header.len,
					  sizeof(ubyte *))) == NULL)
	{
		fprintf(stderr, "Out of memory.\n");
		perror("pdb_LoadResources: malloc");
		return -1;
	}

	/* Read each resource in turn */
	for (i = 0; i < db->reclist_header.len; i++)
	{
		off_t offset;		/* Current offset, for checking */
		udword next_off;	/* Offset of the next thing in the
					 * file */

printf("Reading resource %d\n", i);
		/* Out of paranoia, make sure we're in the right place */
		offset = lseek(fd, 0, SEEK_CUR);
					/* Find out where we are now */
		if (offset != db->rec_index.res[i].offset)
		{
			fprintf(stderr, "Warning: resource %d isn't where I thought it would be.\n"
				"Expected 0x%lx, but we're at 0x%qx\n",
				i,
				db->rec_index.res[i].offset, offset);

			/* Try to recover */
			offset = lseek(fd, db->rec_index.res[i].offset,
				       SEEK_SET);
						/* Go to where this
						 * resource ought to be.
						 */
			if (offset < 0)
			{
				/* Something's wrong */
				fprintf(stderr, "Can't find resource %d\n",
					i);
				return -1;
			}
		}

		/* Okay, now that we're in the right place. Find out what
		 * the next thing in the file is: its offset will tell us
		 * how much to read.
		 */
		if (i == db->reclist_header.len - 1)
		{
			/* This is the last resource in the file, so it
			 * goes to the end of the file.
			 */
			next_off = db->file_size;
		} else {
			/* This isn't the last resource. Find the next
			 * one's offset.
			 */
			next_off = db->rec_index.res[i+1].offset;
		}

		/* Subtract this resource's index from that of the next
		 * thing, to get the size of this resource.
		 */
		db->data_len[i] = next_off - db->rec_index.res[i].offset;

		/* Allocate space for this resource */
		if ((db->data[i] = (ubyte *) malloc(db->data_len[i])) == NULL)
		{
			fprintf(stderr, "Out of memory\n");
			return -1;
		}

		/* Read the resource */
		if ((err = read(fd, db->data[i], db->data_len[i])) !=
		    db->data_len[i])
		{
			fprintf(stderr, "Can't read resource %d\n", i);
			perror("pdb_LoadResources: read");
			return -1;
		}
/*  printf("Contents of resource %d:\n", i); */
/*  debug_dump(stdout, "<RSRC", db->data[i], db->data_len[i]); */
	}

	return 0;		/* Success */
}

static int
pdb_LoadRecords(int fd,
		struct pdb *db)
{
	int i;
	int err;

	/* Allocate an array to hold the records' sizes */
	if ((db->data_len = (uword *) calloc(db->reclist_header.len,
					     sizeof(uword))) == NULL)
	{
		fprintf(stderr, "Out of memory.\n");
		perror("pdb_LoadRecords: malloc");
		return -1;
	}

	/* Allocate an array to hold the records */
	if ((db->data = (ubyte **) calloc(db->reclist_header.len,
					  sizeof(ubyte *))) == NULL)
	{
		fprintf(stderr, "Out of memory.\n");
		perror("pdb_LoadRecords: malloc");
		return -1;
	}

	/* Read each record in turn */
	for (i = 0; i < db->reclist_header.len; i++)
	{
		off_t offset;		/* Current offset, for checking */
		udword next_off;	/* Offset of the next thing in the
					 * file */

printf("Reading record %d\n", i);
		/* Out of paranoia, make sure we're in the right place */
		offset = lseek(fd, 0, SEEK_CUR);
					/* Find out where we are now */
		if (offset != db->rec_index.rec[i].offset)
		{
			fprintf(stderr, "Warning: record %d isn't where I thought it would be.\n"
				"Expected 0x%lx, but we're at 0x%qx\n",
				i,
				db->rec_index.rec[i].offset, offset);

			/* Try to recover */
			offset = lseek(fd, db->rec_index.rec[i].offset,
				       SEEK_SET);
						/* Go to where this
						 * record ought to be.
						 */
			if (offset < 0)
			{
				/* Something's wrong */
				fprintf(stderr, "Can't find record %d\n",
					i);
				return -1;
			}
		}

		/* Okay, now that we're in the right place. Find out what
		 * the next thing in the file is: its offset will tell us
		 * how much to read.
		 */
		if (i == db->reclist_header.len - 1)
		{
			/* This is the last record in the file, so it
			 * goes to the end of the file.
			 */
			next_off = db->file_size;
		} else {
			/* This isn't the last record. Find the next
			 * one's offset.
			 */
			next_off = db->rec_index.rec[i+1].offset;
		}

		/* Subtract this record's index from that of the next
		 * thing, to get the size of this record.
		 */
		db->data_len[i] = next_off - db->rec_index.rec[i].offset;

		/* Allocate space for this record */
		if ((db->data[i] = (ubyte *) malloc(db->data_len[i])) == NULL)
		{
			fprintf(stderr, "Out of memory\n");
			return -1;
		}

		/* Read the record */
		if ((err = read(fd, db->data[i], db->data_len[i])) !=
		    db->data_len[i])
		{
			fprintf(stderr, "Can't read record %d\n", i);
			perror("pdb_LoadRecords: read");
			return -1;
		}
/*  printf("Contents of record %d:\n", i); */
/*  debug_dump(stdout, "<REC", db->data[i], db->data_len[i]); */
	}

	return 0;		/* Success */
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
