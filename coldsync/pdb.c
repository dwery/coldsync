/* pdb.c
 *
 * Functions for dealing with Palm databases and such.
 *
 * $Id: pdb.c,v 1.1 1999-02-19 23:00:14 arensb Exp $
 */
#include <stdio.h>
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

/* read_pdb
 *
 * Read a database from the given file descriptor. Put the header in
 * 'header'.
 */
/* XXX - Document the format of database files */
/* XXX - If we could assume that 'fd' always refers to a file, we could
 * probably just use mmap(), which might be simpler.
 */
/* XXX - A lot of this code is similar for record and resource databases.
 * Is it worth splitting this function into two, one for each type?
 */
/* XXX - It'd probably be a good idea to split this function up by
 * functionality: one function to read the header and index; one to read
 * the app info block; one to read the sort info block; one to read the
 * next record.
 * Another question is, how do you sync the app and sort blocks? Are they
 * just considered a special type of record?
 */
struct pdb *
read_pdb(int fd)
{
	int err;
	struct pdb *retval;
	static ubyte buf[81920];	/* XXX - temporary */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	int i;
	time_t t;
	long file_size;		/* Length of the file (or, technically, the
				 * distance from the current position to
				 * the end of the file, in case the caller
				 * passed us a file that had already been
				 * read from).
				 */
off_t offset;

	/* Find out the length of the file */
	/* (Note: this bare block is here just to localize variables.) */
	{
		off_t here;	/* Current position in the file */
		off_t end;	/* End-of-file position */

		/* Get the current position within the file */
		here = lseek(fd, 0, SEEK_CUR);
		/* XXX - Error-checking: if the file isn't seekable, we have
		 * a problem.
		 */

		/* Go to the end of the file */
		end = lseek(fd, 0, SEEK_END);

		/* And return to where we were before */
		lseek(fd, here, SEEK_SET);

		file_size = end - here;
/*  printf("File size: %ld bytes\n", file_size); */
	}

	/* Allocate the database */
	if ((retval = (struct pdb *) malloc(sizeof(struct pdb))) == NULL)
	{
		return NULL;
	}

	/* Read the database header data */
offset = lseek(fd, 0, SEEK_CUR);
printf("file offset == %qd (0x%qx)\n", offset, offset);
	if ((err = read(fd, buf, PDB_HEADER_LEN)) != PDB_HEADER_LEN)
	{
		/* XXX - Might need better error-checking */
		perror("read_pdb: read");
		free_pdb(retval);
		return NULL;
	}

	/* Parse the database header */
	rptr = buf;
	memcpy(retval->header.name, buf, PDB_DBNAMELEN);
	rptr += PDB_DBNAMELEN;
	retval->header.attributes = get_uword(&rptr);
	retval->header.version = get_uword(&rptr);
	retval->header.ctime = get_udword(&rptr);
	retval->header.mtime = get_udword(&rptr);
	retval->header.baktime = get_udword(&rptr);
	retval->header.modnum = get_udword(&rptr);
	retval->header.appinfoID = get_udword(&rptr);
	retval->header.sortinfoID = get_udword(&rptr);
	retval->header.type = get_udword(&rptr);
	retval->header.creator = get_udword(&rptr);
	retval->header.uniqueIDseed = get_udword(&rptr);

	/* XXX - These printf() statements should be controlled by a
	 * debugging flag.
	 */
	printf("\tname: \"%s\"\n", retval->header.name);
	printf("\tattributes: 0x%04x", retval->header.attributes);
	if (retval->header.attributes & PDB_ATTR_RESDB) printf(" RESDB");
	if (retval->header.attributes & PDB_ATTR_RO) printf(" RO");
	if (retval->header.attributes & PDB_ATTR_APPINFODIRTY)
		printf(" APPINFODIRTY");
	if (retval->header.attributes & PDB_ATTR_BACKUP) printf(" BACKUP");
	if (retval->header.attributes & PDB_ATTR_OKNEWER) printf(" OKNEWER");
	if (retval->header.attributes & PDB_ATTR_RESET) printf(" RESET");
	if (retval->header.attributes & PDB_ATTR_OPEN) printf(" OPEN");
	printf("\n");
	printf("\tversion: %u\n", retval->header.version);
	t = retval->header.ctime - EPOCH_1904;
	printf("\tctime: %lu %s\n", retval->header.ctime,
	       ctime(&t));
	t = retval->header.mtime - EPOCH_1904;
	printf("\tmtime: %lu %s\n", retval->header.mtime,
	       ctime(&t));
	t = retval->header.baktime - EPOCH_1904;
	printf("\tbaktime: %lu %s\n", retval->header.baktime,
	       ctime(&t));
	printf("\tmodnum: %ld\n", retval->header.modnum);
	printf("\tappinfoID: 0x%08lx\n",
	       retval->header.appinfoID);
	printf("\tsortinfoID: 0x%08lx\n",
	       retval->header.sortinfoID);
	printf("\ttype: '%c%c%c%c' (0x%08lx)\n",
	       (char) (retval->header.type >> 24) & 0xff,
	       (char) (retval->header.type >> 16) & 0xff,
	       (char) (retval->header.type >> 8) & 0xff,
	       (char) retval->header.type & 0xff,
	       retval->header.type);
	printf("\tcreator: '%c%c%c%c' (0x%08lx)\n",
	       (char) (retval->header.creator >> 24) & 0xff,
	       (char) (retval->header.creator >> 16) & 0xff,
	       (char) (retval->header.creator >> 8) & 0xff,
	       (char) retval->header.creator & 0xff,
	       retval->header.creator);
	printf("\tuniqueIDseed: %ld\n", retval->header.uniqueIDseed);

	/* Read the record list to find out how many records there are in
	 * the file.
	 */
offset = lseek(fd, 0, SEEK_CUR);
printf("file offset == %qd (0x%qx)\n", offset, offset);
	if ((err = read(fd, buf, PDB_RECORDLIST_LEN)) < 0)
	{
		/* XXX - Probably need better error-checking */
		perror("read_pdb: read2");
		free_pdb(retval);
		return NULL;
	}

	printf("Record list:\n");
	rptr = buf;
	retval->reclist_header.nextID = get_udword(&rptr);
	retval->reclist_header.len = get_uword(&rptr);

	printf("\tnextID: %ld\n", retval->reclist_header.nextID);
	printf("\tlen: %u\n", retval->reclist_header.len);

	/* Allocate space for the record index */
	if (IS_RSRC_DB(retval))
	{
		/* It's a resource database */
		if ((retval->rec_index.res =
		     (struct pdb_resource *) calloc(retval->reclist_header.len,
						    sizeof(struct pdb_resource)))
		    == NULL)
		{
			free_pdb(retval);
			return NULL;
		}
	} else {
		/* It's a record database */
		if ((retval->rec_index.rec =
		     (struct pdb_record *) calloc(retval->reclist_header.len,
						  sizeof(struct pdb_record)))
		    == NULL)
		{
			free_pdb(retval);
			return NULL;
		}
	}

	/* Read the record index */
offset = lseek(fd, 0, SEEK_CUR);
printf("file offset == %qd (0x%qx)\n", offset, offset);
	if (IS_RSRC_DB(retval))
	{
		err = read(fd, buf, PDB_RESOURCEIX_LEN *
			   retval->reclist_header.len);
	} else {
		err = read(fd, buf, PDB_RECORDIX_LEN *
		     retval->reclist_header.len);
debug_dump(stdout, "RecIx", buf, PDB_RECORDIX_LEN * retval->reclist_header.len);
	}
	/* XXX - Probably need better error-checking */
	if (err < 0)
	{
		free_pdb(retval);
		return NULL;
	}

	rptr = buf;
	for (i = 0; i < retval->reclist_header.len; i++)
		if (IS_RSRC_DB(retval))
		{
			/* It's a resource database. Read resource entries.
			 */
			retval->rec_index.res[i].type = get_udword(&rptr);
			retval->rec_index.res[i].id = get_uword(&rptr);
			retval->rec_index.res[i].offset = get_udword(&rptr);

			printf("\t+ Resource %u\n", i);
			printf("\ttype: '%c%c%c%c' (0x%08lx)\n",
			       (char) (retval->rec_index.res[i].type >> 24) & 0xff,
			       (char) (retval->rec_index.res[i].type >> 16) & 0xff,
			       (char) (retval->rec_index.res[i].type >> 8) & 0xff,
			       (char) retval->rec_index.res[i].type & 0xff,
				       retval->rec_index.res[i].type);
			printf("\tid: %u\n", retval->rec_index.res[i].id);
			printf("\toffset: 0x%08lx\n", retval->rec_index.res[i].offset);
			/* XXX - Make sure they're in order */
			printf("\n");
		} else {
			/* It's a record database. Read record entries */

			retval->rec_index.rec[i].offset = get_udword(&rptr);
			retval->rec_index.rec[i].attributes = get_ubyte(&rptr);
/*  			memcpy(&(retval->rec_index.rec[i].uniqueID), &rptr, 3); */
			memcpy(&(retval->rec_index.rec[i].uniqueID[0]), rptr, 3);
			rptr += 3;

			printf("\t* Record %d\n", i);
			printf("\toffset: 0x%08lx\n",
			       retval->rec_index.rec[i].offset);
			printf("\tattributes: 0x%02x\n",
			       retval->rec_index.rec[i].attributes);
			printf("\tuniqueID: 0x%02x%02x%02x\n",
			       retval->rec_index.rec[i].uniqueID[0],
			       retval->rec_index.rec[i].uniqueID[1],
			       retval->rec_index.rec[i].uniqueID[2]);
			/* XXX - Make sure they're in order */
			printf("\n");
		}

offset = lseek(fd, 0, SEEK_CUR);
printf("file offset == %qd (0x%qx)\n", offset, offset);
	/* Skip the two NUL bytes */
	if ((err = read(fd, buf, 2)) < 0)
	{
		/* XXX - Better error-checking? */
		perror("read_pdb: read3");
		free_pdb(retval);
		return NULL;
	}
	printf("2 NULs: 0x%02x 0x%02x\n", buf[0], buf[1]);
offset = lseek(fd, 0, SEEK_CUR);
printf("file offset == %qd (0x%qx)\n", offset, offset);

	/* Read the app info block, if there is one. */
	if (retval->header.appinfoID != 0)
	{
		/* XXX - Should this be a 'long', or is there a better
		 * type?
		 */
		long appinfo_len;	/* Length of app info block */

		/* Figure out the length of the app info block: see what
		 * comes next, sort info block or records, and use the
		 * difference in offsets.
		 */
		if (retval->header.sortinfoID != 0)
			/* There's a sort info block. Use its offset to get
			 * the length.
			 */
			appinfo_len = retval->header.sortinfoID -
				retval->header.appinfoID;
		else if (retval->reclist_header.len > 0)
		{
			/* There's at least one record. Use the first one's
			 * offset to get the length of the app info block.
			 */
			if (IS_RSRC_DB(retval))
				/* Use the offset of the first resource */
				appinfo_len = retval->rec_index.res[0].offset -
					retval->header.appinfoID;
			else
				/* Use the offset of the first record */
				appinfo_len = retval->rec_index.rec[0].offset -
					retval->header.appinfoID;
		} else {
			/* There's neither a sort info block nor records.
			 * The app info block goes to the end of the file.
			 */
			appinfo_len = file_size - retval->header.appinfoID;
		}
printf("appinfo_len == %lu (0x%04lx)\n", appinfo_len, appinfo_len);

		/* Allocate space to hold the app info block */
		if ((retval->appinfo = malloc(appinfo_len)) == NULL)
		{
			/* Out of memory */
			free_pdb(retval);
			return NULL;
		}

		/* Read the app info block */
		if ((err = read(fd, retval->appinfo, appinfo_len)) < 0)
		{
			/* XXX - Try to reread if managed to read
			 * something, but not the entire block?
			 */
			free_pdb(retval);
			return NULL;
		}
debug_dump(stdout, "AppInfo", retval->appinfo, appinfo_len);
	}

	/* Read the sort block, if there is one */
	/* XXX - This bit is pretty much untested. Need to find some
	 * databases with sort blocks to test it with.
	 */
	if (retval->header.sortinfoID != 0)
	{
		/* XXX - Should this be a 'long', or is there a better
		 * type?
		 */
		long sortinfo_len;	/* Length of sort info block */

		/* Figure out the length of the sort block: get the offset
		 * of the first data record, and use the difference in
		 * offsets.
		 */
		if (retval->reclist_header.len > 0)
		{
			/* There's at least one record. Use the first one's
			 * offset to get the length of the sort block.
			 */
			if (IS_RSRC_DB(retval))
				/* Use the offset of the first resource */
				sortinfo_len = retval->rec_index.res[0].offset -
					retval->header.sortinfoID;
			else
				/* Use the offset of the first record */
				sortinfo_len = retval->rec_index.rec[0].offset -
					retval->header.sortinfoID;
		} else {
			/* There aren't any records. The sort info block
			 * goes to the end of the file.
			 */
			sortinfo_len = file_size - retval->header.sortinfoID;
		}
printf("sortinfo_len == %lu (0x%04lx)\n", sortinfo_len, sortinfo_len);

		/* Allocate space to hold the sort info block */
		if ((retval->sortinfo = malloc(sortinfo_len)) == NULL)
		{
			/* Out of memory */
			free_pdb(retval);
			return NULL;
		}

		/* Read the sort info block */
		if ((err = read(fd, retval->sortinfo, sortinfo_len)) < 0)
		{
			/* XXX - Try to reread if managed to read
			 * something, but not the entire block?
			 */
			free_pdb(retval);
			return NULL;
		}
debug_dump(stdout, "SortInfo", retval->sortinfo, sortinfo_len);
	}

	/* Read the records */
	/* XXX - This seems to work fine most of the time, but appears to
	 * break with BattleShip.prc (or resource databases in general).
	 * Check it.
	 */
	for (i = 0; i < retval->reclist_header.len; i++)
	{
		/* XXX - Should this be a 'long', or is there a better
		 * type?
		 */
		long record_len;	/* Length of this record */
		long cur_offset;	/* Offset of this record */

		/* Figure out the record length from its offset and that of
		 * the next record.
		 */
		/* Get the offset of the current record */
		if (IS_RSRC_DB(retval))
			cur_offset = retval->rec_index.res[i].offset;
		else
			cur_offset = retval->rec_index.rec[i].offset;
		if (i == retval->reclist_header.len - 1)
		{
			/* This is the last record. It goes to the end of
			 * the file.
			 */
			record_len = file_size - cur_offset;
		} else {
			/* This is not the last record. Get the offset from
			 * the next record.
			 */
			if (IS_RSRC_DB(retval))
				record_len = retval->rec_index.res[i+1].offset -
					cur_offset;
			else
				record_len = retval->rec_index.rec[i+1].offset -
					cur_offset;
		}
printf("Record %d len == %ld\n", i, record_len);

		/* Read the record */
		/* XXX - I'm not quite sure we really want to read the
		 * entire database (including all the records) into memory
		 * at once. I'm doing it here to prove to myself that I
		 * understand the format of the file, but it probably
		 * shouldn't happen in real life.
		 */
		err = read(fd, buf, record_len);
		/* XXX - Error-checking */

		printf("Record %d:\n", i);
		debug_dump(stdout, "Rec", buf, record_len);
	}

/* Paranoia: get the current offset, and see if we're really at the end of
 * the file.
 */
/* XXX - Actually, this doesn't work if there was leading stuff at the
 * beginning of the file.
 */
offset = lseek(fd, 0, SEEK_CUR);
if (offset != file_size)
printf("##### Trailing garbage! cur == %qd, eof == %ld\n", offset, file_size);

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

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
