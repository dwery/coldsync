/* pdb.c
 *
 * Functions for dealing with Palm databases and such.
 *
 * $Id: pdb.c,v 1.9 1999-03-11 10:04:39 arensb Exp $
 */
#include <stdio.h>
#include <fcntl.h>		/* For open() */
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/param.h>		/* For MAXPATHLEN */
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>		/* For strncat() et al. */
#include "palm/palm_types.h"
#include "pconn/util.h"
#include "pdb.h"

/* XXX - Need functions:
 * int write_pdb(fd, struct pdb *pdb) - Write a struct pdb to a file. It'd
 *	be better to have the backup stuff read a database into a struct
 *	pdb, then have write_pdb() (or whatever it's called) do the actual
 *	writing.
 */

/* Helper functions */
static uword get_file_length(int fd);
static int pdb_LoadHeader(int fd, struct pdb *db);
static int pdb_LoadRecListHeader(int fd, struct pdb *db);
static int pdb_LoadRsrcIndex(int fd, struct pdb *db);
static int pdb_LoadRecIndex(int fd, struct pdb *db);
static int pdb_LoadAppBlock(int fd, struct pdb *db);
static int pdb_LoadSortBlock(int fd, struct pdb *db);
static int pdb_LoadResources(int fd, struct pdb *db);
static int pdb_LoadRecords(int fd, struct pdb *db);
static int pdb_DownloadResources(struct PConnection *pconn,
				 ubyte dbh,
				 struct pdb *db);
static int pdb_DownloadRecords(struct PConnection *pconn,
			       ubyte dbh,
			       struct pdb *db);
/* XXX - Document the format of database files */

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

	/* Write zeros all over it, just for safety */
	memset(retval, 0, sizeof(struct pdb));

	return retval;
}

/* free_pdb
 * Cleanly free a struct pdb, and all of its subparts (destructor).
 */
void
free_pdb(struct pdb *db)
{
	if (db == NULL)
		/* Trivial case */
		return;

	/* Free the array of records/resources */
	if (IS_RSRC_DB(db))
	{
		/* It's a resource database */
		struct pdb_resource *rec;
		struct pdb_resource *next;

		/* Walk the linked list, freeing as we go along */
		for (rec = db->rec_index.res;
		     rec != NULL;
		     rec = next)
		{
			next = rec->next;	/* Remember the next
						 * element on the list. We
						 * won't have a chance to
						 * look it up after this
						 * one has been free()d.
						 */

			/* Free the resource data, if any */
			if (rec->data != NULL)
				free(rec->data);

			/* Free this element */
			free(rec);
		}
	} else {
		/* It's a record database */
		struct pdb_record *rec;
		struct pdb_record *next;

		/* Walk the linked list, freeing as we go along */
		for (rec = db->rec_index.rec;
		     rec != NULL;
		     rec = next)
		{
			next = rec->next;	/* Remember the next
						 * element on the list. We
						 * won't have a chance to
						 * look it up after this
						 * one has been free()d.
						 */

			/* Free the record data, if any */
			if (rec->data != NULL)
				free(rec->data);

			/* Free this element */
			free(rec);
		}
	}

	/* Free the sort block */
	if (db->sortinfo != NULL)
		free(db->sortinfo);

	/* Free the app info block */
	if (db->appinfo != NULL)
		free(db->appinfo);

	free(db);
}

/* XXX - Separate pdb_Read() into two functions: one to load the header and
 * record index, and one to read the actual data. This should simplify
 * syncing, since you can have a list of databases on disk (and their
 * relevant characteristics, like creator and type) without having to load
 * their entire contents.
 */
struct pdb *
pdb_Read(char *fname)
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

	close(fd);

	return retval;			/* Success */
}

/* pdb_Write
 * Write 'db' to the file given by 'fname'.
 */
int
pdb_Write(const struct pdb *db,
	  const char *fname)
{
	int err;
	int fd;			/* File descriptor for staging file */
	static char tempfname[MAXPATHLEN];
				/* Name of "staging" file. The database
				 * will be written to this file first; only
				 * after the "staging" file has been
				 * written will it replace the existing
				 * one. This is so that if something goes
				 * wrong, the existing file doesn't get
				 * trashed.
				 */
	static ubyte header_buf[PDB_HEADER_LEN];
				/* Buffer for writing database header */
	static ubyte rlheader_buf[PDB_RECORDLIST_LEN];
				/* Buffer for writing the record list header */
	static ubyte nul_buf[2];
				/* Buffer for writing the two useless NULs */
	ubyte *wptr;		/* Pointer into buffers, for writing */
	udword offset;		/* The next offset we're interested in */

	/* Create the staging file */
	/* Create the staging file name */
	strncpy(tempfname, fname, MAXPATHLEN-7);
	strncat(tempfname, ".XXXXXX", 7);
				/* XXX - The temp file extension and its
				 * length ought to become preprocessor
				 * symbols. */
fprintf(stderr, "Creating staging file \"%s\"\n", tempfname);
	if ((fd = mkstemp(tempfname)) < 0)
	{
		perror("pdb_Write: mkstemp");
		return -1;
	}
	/* XXX - Lock the file. Use fcntl, since it's the most
	 * reliable method.
	 */

	/* Initialize 'offset': the next variable-sized item will go after
	 * the header, after the index header, after the index, after the
	 * two useless NULs.
	 */
	offset = PDB_HEADER_LEN + PDB_RECORDLIST_LEN;
	if (IS_RSRC_DB(db))
		offset += db->numrecs * PDB_RESOURCEIX_LEN;
	else
		offset += db->numrecs * PDB_RECORDIX_LEN;
	offset += 2;		/* Those two useless NUL bytes */

	/** Write the database header **/

	/* Construct the header in 'header_buf' */
	wptr = header_buf;
	memcpy(wptr, db->name, PDB_DBNAMELEN);
	wptr += PDB_DBNAMELEN;
	put_uword(&wptr, (db->attributes & ~DLPCMD_DBFLAG_OPEN));
				/* Clear the 'open' flag before writing */
	put_uword(&wptr, db->version);
	put_udword(&wptr, db->ctime);
	put_udword(&wptr, db->mtime);
	put_udword(&wptr, db->baktime);
	put_udword(&wptr, db->modnum);
	if (db->appinfo == NULL)	/* Write the AppInfo block, if any */
		/* This database doesn't have an AppInfo block */
		put_udword(&wptr, 0L);
	else {
		/* This database has an AppInfo block */
		put_udword(&wptr, offset);
		offset += db->appinfo_len;
	}
	if (db->sortinfo == NULL)	/* Write the sort block, if any */
		/* This database doesn't have a sort block */
		put_udword(&wptr, 0L);
	else {
		put_udword(&wptr, offset);
		offset += db->sortinfo_len;
	}
	put_udword(&wptr, db->type);
	put_udword(&wptr, db->creator);
	put_udword(&wptr, db->uniqueIDseed);

	/* Write the database header */
	if (write(fd, header_buf, PDB_HEADER_LEN) != PDB_HEADER_LEN)
	{
		fprintf(stderr, "pdb_Write: can't write database header\n");
		perror("write");
		close(fd);
		return -1;
	}

	/** Write the record/resource index header **/
	/* Construct the record list header */
	wptr = rlheader_buf;
	put_udword(&wptr, 0L);	/* nextID */
			/* XXX - What is this? Should this be something
			 * other than 0? */
	put_uword(&wptr, db->numrecs);

	/* Write the record list header */
	if (write(fd, rlheader_buf, PDB_RECORDLIST_LEN) != PDB_RECORDLIST_LEN)
	{
		fprintf(stderr, "pdb_Write: can't write record list header\n");
		perror("write");
		close(fd);
		return -1;
	}

	/* Write the record/resource index */
	if (IS_RSRC_DB(db))
	{
		/* It's a resource database */
		struct pdb_resource *res;	/* Current resource */

		/* Go through the list of resources, writing each one */
		for (res = db->rec_index.res; res != NULL; res = res->next)
		{
			static ubyte rsrcbuf[PDB_RESOURCEIX_LEN];
					/* Buffer to hold the resource
					 * index entry.
					 */

			/* Construct the resource index entry */
			wptr = rsrcbuf;
			put_udword(&wptr, res->type);
			put_uword(&wptr, res->id);
			put_udword(&wptr, offset);

			/* Write the resource index entry */
			if (write(fd, rsrcbuf, PDB_RESOURCEIX_LEN) !=
			    PDB_RESOURCEIX_LEN)
			{
				fprintf(stderr, "pdb_Write: Can't write resource index entry\n");
				perror("write");
				close(fd);
				return -1;
			}

			/* Bump 'offset' up to point to the offset of the
			 * next variable-sized thing in the file.
			 */
			offset += res->data_len;
		}
	} else {
		/* It's a record database */
		struct pdb_record *rec;		/* Current record */

		/* Go through the list of resources, writing each one */
		for (rec = db->rec_index.rec; rec != NULL; rec = rec->next)
		{
			static ubyte recbuf[PDB_RECORDIX_LEN];
					/* Buffer to hold the record index
					 * entry.
					 */

			/* Construct the record index entry */
			wptr = recbuf;
			put_udword(&wptr, offset);
			put_ubyte(&wptr, rec->attributes);
			put_ubyte(&wptr,
				  (char) ((rec->uniqueID >> 16) & 0xff));
			put_ubyte(&wptr,
				  (char) ((rec->uniqueID >> 8) & 0xff));
			put_ubyte(&wptr,
				  (char) (rec->uniqueID & 0xff));

			/* Write the resource index entry */
			if (write(fd, recbuf, PDB_RECORDIX_LEN) !=
			    PDB_RECORDIX_LEN)
			{
				fprintf(stderr, "pdb_Write: Can't write RECORD index entry\n");
				perror("write");
				close(fd);
				return -1;
			}

			/* Bump 'offset' up to point to the offset of the
			 * next variable-sized thing in the file.
			 */
			offset += rec->data_len;
		}
	}

	/* Write the two useless NUL bytes */
	nul_buf[0] = nul_buf[1] = '\0';
	if (write(fd, nul_buf, 2) != 2)
	{
		fprintf(stderr, "pdb_Write: Can't write the two useless NULs\n");
		perror("write");
		close(fd);
		return -1;
	}

	/* Write the AppInfo block, if any */
	if (db->appinfo != NULL)
	{
		if (write(fd, db->appinfo, db->appinfo_len) !=
		    db->appinfo_len)
		{
			fprintf(stderr, "pdb_Write: Can't write AppInfo block\n");
			perror("write");
			close(fd);
			return -1;
		}
	}

	/* Write the sort block, if any */
	if (db->sortinfo != NULL)
	{
		if (write(fd, db->sortinfo, db->sortinfo_len) !=
		    db->sortinfo_len)
		{
			fprintf(stderr, "pdb_Write: Can't write sort block\n");
			perror("write");
			close(fd);
			return -1;
		}
	}

	/* Write the record/resource data */
	if (IS_RSRC_DB(db))
	{
		/* It's a resource database */
		struct pdb_resource *res;

		/* Go through the list of resources, writing each one's
		 * data.
		 */
		for (res = db->rec_index.res; res != NULL; res = res->next)
		{
			/* Write the data */
			if (write(fd, res->data, res->data_len) !=
			    res->data_len)
			{
				fprintf(stderr, "pdb_Write: Can't write resource data\n");
				perror("write");
				close(fd);
				return -1;
			}
		}
	} else {
		/* It's a record database */
		struct pdb_record *rec;

		/* Go through the list of records, writing each one's data. */
		for (rec = db->rec_index.rec; rec != NULL; rec = rec->next)
		{
			/* Write the data */
			if (write(fd, rec->data, rec->data_len) !=
			    rec->data_len)
			{
				fprintf(stderr, "pdb_Write: Can't write record data\n");
				perror("write");
				close(fd);
				return -1;
			}
		}
	}

	/* If everything successful so far, rename the staging file to the
	 * real file.
	 */
	err = rename(tempfname, fname);
	if (err < 0)
	{
		fprintf(stderr, "Error renaming \"%s\" to \"%s\"\n",
			tempfname, fname);
		perror("rename");
		close(fd);
		return -1;	/* XXX - Not a show-stopper */
	}

	/* XXX - Unlock the file */

	close(fd);		/* Clean up */

	return 0;		/* Success */
}

/* pdb_Download
 * Download a database from the Palm. The returned 'struct pdb' is
 * allocated by pdb_Download(), and the caller has to free it.
 */
struct pdb *
pdb_Download(struct PConnection *pconn,
	     const struct dlp_dbinfo *dbinfo,
	     ubyte dbh)		/* Database handle */
{
	int err;
	struct pdb *retval;
	const ubyte *rptr;	/* Pointer into buffers, for reading */
		/* These next two variables are here mainly to make the
		 * types come out right.
		 */
	uword appinfo_len;	/* Length of AppInfo block */
	uword sortinfo_len;	/* Length of sort block */
	struct dlp_opendbinfo opendbinfo;
				/* Info about open database (well, the # of
				 * resources in it). */

	/* Allocate the return value */
	if ((retval = new_pdb()) == NULL)
	{
		fprintf(stderr, "pdb_Download: can't allocate pdb\n");
		return NULL;
	}

	/* Get the database header info */
	memcpy(retval->name, dbinfo->name, PDB_DBNAMELEN);
	retval->attributes = dbinfo->db_flags;
	retval->version = dbinfo->version;
	/* Convert the times from DLP time structures to Palm-style
	 * time_ts.
	 */
	retval->ctime = time_dlp2palmtime(&dbinfo->ctime);
	retval->mtime = time_dlp2palmtime(&dbinfo->mtime);
	retval->baktime = time_dlp2palmtime(&dbinfo->baktime);
	retval->modnum = dbinfo->modnum;
	retval->appinfo_offset = 0L;	/* For now */
	retval->sortinfo_offset = 0L;	/* For now */
	retval->type = dbinfo->type;
	retval->creator = dbinfo->creator;
	retval->uniqueIDseed = 0L;	/* XXX - Should this be something
					 * else? */
fprintf(stderr, "pdb_Download:\n");
fprintf(stderr, "\tname: \"%s\"\n", retval->name);
fprintf(stderr, "\tattributes: 0x%04x\n", retval->attributes);
fprintf(stderr, "\tversion: %d\n", retval->version);
fprintf(stderr, "\tctime: %ld\n", retval->ctime);
fprintf(stderr, "\tmtime: %ld\n", retval->mtime);
fprintf(stderr, "\tbaktime: %ld\n", retval->baktime);
fprintf(stderr, "\tmodnum: %ld\n", retval->modnum);
fprintf(stderr, "\tappinfo_offset: %ld\n", retval->appinfo_offset);
fprintf(stderr, "\tsortinfo_offset: %ld\n", retval->sortinfo_offset);
fprintf(stderr, "\ttype: '%c%c%c%c'\n",
	(char) ((retval->type >> 24) & 0xff),
	(char) ((retval->type >> 16) & 0xff),
	(char) ((retval->type >> 8) & 0xff),
	(char) (retval->type & 0xff));
fprintf(stderr, "\tcreator: '%c%c%c%c'\n",
	(char) ((retval->creator >> 24) & 0xff),
	(char) ((retval->creator >> 16) & 0xff),
	(char) ((retval->creator >> 8) & 0xff),
	(char) (retval->creator & 0xff));
fprintf(stderr, "\tuniqueIDseed: %ld\n", retval->uniqueIDseed);

	/* Get the database record/resource index header info */
	/* Find out how many records/resources there are in this database */
	err = DlpReadOpenDBInfo(pconn, dbh, &opendbinfo);
	if (err != DLPSTAT_NOERR)
	{
		fprintf(stderr, "pdb_Download: Can't read database info: %d\n",
			err);
		DlpCloseDB(pconn, dbh);	/* Don't really care if this fails */
		free_pdb(retval);
		return NULL;
	}
	retval->next_reclistID = 0L;
	retval->numrecs = opendbinfo.numrecs;
fprintf(stderr, "\n\tnextID: %ld\n", retval->next_reclistID);
fprintf(stderr, "\tlen: %d\n", retval->numrecs);

	/* Try to get the AppInfo block */
	err = DlpReadAppBlock(pconn, dbh, 0, DLPC_APPBLOCK_TOEND,
			      &appinfo_len, &rptr);
	switch (err)
	{
	    case DLPSTAT_NOERR:
		/** Make a copy of the AppInfo block **/
		/* Allocate space for the AppInfo block */
		if ((retval->appinfo = (ubyte *) malloc(appinfo_len))
		    == NULL)
		{
			fprintf(stderr, "** Out of memory\n");
			DlpCloseDB(pconn, dbh);	/* Don't really care if
						 * this fails */
			free_pdb(retval);
			return NULL;
		}
		memcpy(retval->appinfo, rptr, appinfo_len);
					/* Copy the AppInfo block */
		retval->appinfo_len = appinfo_len;
fprintf(stderr, "pdb_Download: got an AppInfo block\n");
/*  debug_dump(stderr, "APP", retval->appinfo, retval->appinfo_len); */
		break;
	    case DLPSTAT_NOTFOUND:
		/* This database doesn't have an AppInfo block */
		retval->appinfo_len = 0;
		retval->appinfo = NULL;
fprintf(stderr, "pdb_Download: this db doesn't have an AppInfo block\n");
		break;
	    default:
		fprintf(stderr, "*** Can't read AppInfo block for %s: %d\n",
			dbinfo->name, err);
		DlpCloseDB(pconn, dbh);	/* Don't really care if this fails */
		free_pdb(retval);
		return NULL;
	}

	/* Try to get the sort block */
	err = DlpReadSortBlock(pconn, dbh, 0, DLPC_SORTBLOCK_TOEND,
			       &sortinfo_len, &rptr);
	switch (err)
	{
	    case DLPSTAT_NOERR:
		/** Make a copy of the sort block **/
		/* Allocate space for the sort block */
		if ((retval->sortinfo = (ubyte *) malloc(sortinfo_len))
		    == NULL)
		{
			fprintf(stderr, "** Out of memory\n");
			DlpCloseDB(pconn, dbh);	/* Don't really care if
						 * this fails */
			free_pdb(retval);
			return NULL;
		}
		memcpy(retval->sortinfo, rptr, retval->sortinfo_len);
					/* Copy the sort block */
		retval->sortinfo_len = sortinfo_len;
		break;
	    case DLPSTAT_NOTFOUND:
		/* This database doesn't have a sort block */
		retval->sortinfo_len = 0;
		retval->sortinfo = NULL;
		break;
	    default:
		fprintf(stderr, "*** Can't read sort block for %s: %d\n",
			dbinfo->name, err);
		DlpCloseDB(pconn, dbh);	/* Don't really care if this fails */
		free_pdb(retval);
		return NULL;
	}

	/* Download the records/resources */
	if (DBINFO_ISRSRC(dbinfo))
		err = pdb_DownloadResources(pconn, dbh, retval);
	else
		err = pdb_DownloadRecords(pconn, dbh, retval);

	if (err < 0)
	{
		fprintf(stderr, "Can't download record or resource index\n");
		DlpCloseDB(pconn, dbh);
		free_pdb(retval);
		return NULL;
	}

	return retval;			/* Success */
}

/* pdb_FindRecordByID
 * Find the record in 'db' whose uniqueID is 'id'. Return a pointer to it.
 * If no such record exists, or in case of error, returns NULL.
 */
struct pdb_record *
pdb_FindRecordByID(
	const struct pdb *db,
	const udword id)
{
	struct pdb_record *rec;

	/* Walk the list of records, comparing IDs */
	for (rec = db->rec_index.rec; rec != NULL; rec = rec->next)
	{
		if (rec->uniqueID == id)
			return rec;
	}

	return NULL;		/* Couldn't find it */
}

/* pdb_FindRecordByIndex
 * Find the 'index'th record in 'db', and return a pointer to it. If no
 * such record exists, or in case of error, return NULL.
 */
struct pdb_record *
pdb_FindRecordByIndex(
	const struct pdb *db,	/* Database to look in */
	const uword index)	/* Index of the record to look for */
{
	struct pdb_record *rec;
	int i;

	/* Walk the list, decrementing the count as we go along. If it
	 * reaches 0, we've found the record.
	 */
	rec = db->rec_index.rec;
	for (i = index; i > 0; i--)
	{
		if (rec == NULL)
			/* Oops! We've fallen off the end of the list */
			return NULL;
		rec = rec->next;
	}

	return rec;		/* Success */
}

/* pdb_NextRecord
 * Find the next record after 'rec' in 'db', and return a pointer to it. If
 * 'rec' is the last record in the list, return NULL.
 */
struct pdb_record *
pdb_NextRecord(const struct pdb *db,	/* Database to look in */
	       const struct pdb_record *rec)
					/* Return 'rec's successor */
{
	return rec->next;
}

/* pdb_DeleteRecordByID
 * Find the record whose unique ID is 'id' and delete it from 'db'. If the
 * record isn't found, well, that's okay; we wanted to delete it anyway.
 * Returns 0 if successful, -1 in case of error.
 */
/* XXX - This really ought to be redone with a linked list */
int
pdb_DeleteRecordByID(
	struct pdb *db,
	const udword id)
{
	struct pdb_record *rec;		/* Record we're looking at */
	struct pdb_record *last;	/* Last record we saw */

	if (IS_RSRC_DB(db))
		/* This only works with record databases */
		return -1;

	/* Look through the list of records */
	last = NULL;		/* Haven't seen any records yet */
	for (rec = db->rec_index.rec; rec != NULL; rec = rec->next)
	{
		/* See if the uniqueID matches */
		if (rec->uniqueID == id)
		{
			/* Found it */

			/* Free 'rec's data */
			if (rec->data != NULL)
				free(rec->data);

			/* Cut 'rec' out of the list. The first element of
			 * the list is a special case.
			 */
			if (last == NULL)
				db->rec_index.rec = rec->next;
			else
				last->next = rec->next;

			free(rec);		/* Free it */
			db->numrecs--;		/* Decrement record count */
			
			return 0;	/* Success */
		}

		last = rec;	/* Remember what we just saw */
	}

	/* Couldn't find it. Oh, well. Call it a success anyway. */
	return 0;
}

/* pdb_AppendRecord
 * Append a new record to 'db's record list. 'newrec' is not copied, so it
 * is important that the caller not free it afterwards.
 */
int
pdb_AppendRecord(struct pdb *db,
		 struct pdb_record *newrec)
{
	struct pdb_record *rec;

	/* Sanity check */
	if (IS_RSRC_DB(db))
		/* This only works with record databases */
		return -1;

	/* Check to see if the list is empty */
	if (db->rec_index.rec == NULL)
	{
		/* XXX - Sanity check: db->numrecs should be 0 */
		db->rec_index.rec = newrec;
		newrec->next = NULL;

		return 0;		/* Success */
	}

	/* Walk the list to find its end */
	for (rec = db->rec_index.rec; rec->next != NULL; rec = rec->next)
		;
	rec->next = newrec;
	newrec->next = NULL;

	return 0;			/* Success */
}

/* pdb_AppendResource
 * Append a new resource to 'db's resource list. 'newrsrc' is not copied,
 * so it is important that the caller not free it afterwards.
 */
int
pdb_AppendResource(struct pdb *db,
		   struct pdb_resource *newrsrc)
{
	struct pdb_resource *rsrc;

	/* Sanity check */
	if (!IS_RSRC_DB(db))
		/* This only works with resource databases */
		return -1;

	/* Check to see if the list is empty */
	if (db->rec_index.res == NULL)
	{
		/* XXX - Sanity check: db->numrecs should be 0 */
		db->rec_index.res = newrsrc;
		newrsrc->next = NULL;

		return 0;		/* Success */
	}

	/* Walk the list to find its end */
	for (rsrc = db->rec_index.res; rsrc->next != NULL; rsrc = rsrc->next)
		;
	rsrc->next = newrsrc;
	newrsrc->next = NULL;

	return 0;			/* Success */
}

/* pdb_InsertRecord
 * Insert 'newrec' into 'db', just after 'prev'. If 'prev' is NULL,
 * 'newrec' is inserted at the beginning of the list.
 * Returns 0 if successful, -1 otherwise.
 * 'newrec' is not copied, so it is important that the caller not free it.
 */
int
pdb_InsertRecord(struct pdb *db,	/* The database to insert into */
		 struct pdb_record *prev,
					/* Insert after this record */
		 struct pdb_record *newrec)
					/* The record to insert */
{
	/* If 'prev' is NULL, insert at the beginning of the list */
	if (prev == NULL)
	{
		newrec->next = db->rec_index.rec;
		db->rec_index.rec = newrec;
		db->numrecs++;		/* Increment record count */

		return 0;		/* Success */
	}

	/* XXX - This function doesn't actually check to make sure that
	 * 'prev' is in 'db'. You could really fuck yourself over with
	 * this.
	 */
	/* The new record goes in the middle of the list. Insert it. */
	newrec->next = prev->next;
	prev->next = newrec;
	db->numrecs++;			/* Increment record count */

	return 0;			/* Success */
}

/* pdb_InsertResource
 * Insert 'newrsrc' into 'db', just after 'prev'. If 'prev' is NULL, 'newrsrc'
 * is inserted at the beginning of the list.
 * Returns 0 if successful, -1 otherwise.
 * 'newrec' is not copied, so it is important that the caller not free it.
 */
int
pdb_InsertResource(struct pdb *db,	/* The database to insert into */
		   struct pdb_resource *prev,
					/* Insert after this resource */
		   struct pdb_resource *newrsrc)
					/* The resource to insert */
{
	/* If 'prev' is NULL, insert at the beginning of the list */
	if (prev == NULL)
	{
		newrsrc->next = db->rec_index.res;
		db->rec_index.res = newrsrc;
		db->numrecs++;		/* Increment record count */

		return 0;		/* Success */
	}

	/* XXX - This function doesn't actually check to make sure that
	 * 'prev' is in 'db'. You could really fuck yourself over with
	 * this.
	 */
	/* The new resource goes in the middle of the list. Insert it. */
	newrsrc->next = prev->next;
	prev->next = newrsrc;
	db->numrecs++;			/* Increment record count */

	return 0;			/* Success */
}

/*** Helper functions ***/

/* get_file_length
 * Return the length of a file, in bytes.
 */
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

/* pdb_LoadHeader
 * Read the header of a pdb file, and fill in the appropriate fields in
 * 'db'.
 */
static int
pdb_LoadHeader(int fd,
	       struct pdb *db)
{
	int err;
	static ubyte buf[PDB_HEADER_LEN];
				/* Buffer to hold the file header */
	const ubyte *rptr;	/* Pointer into buffers, for reading */
/*  time_t t; */

	/* Read the header */
	if ((err = read(fd, buf, PDB_HEADER_LEN)) != PDB_HEADER_LEN)
	{
		perror("pdb_LoadHeader: read");
		return -1;
	}

	/* Parse the database header */
	rptr = buf;
	memcpy(db->name, buf, PDB_DBNAMELEN);
	rptr += PDB_DBNAMELEN;
	db->attributes = get_uword(&rptr);
	db->version = get_uword(&rptr);
	db->ctime = get_udword(&rptr);
	db->mtime = get_udword(&rptr);
	db->baktime = get_udword(&rptr);
	db->modnum = get_udword(&rptr);
	db->appinfo_offset = get_udword(&rptr);
	db->sortinfo_offset = get_udword(&rptr);
	db->type = get_udword(&rptr);
	db->creator = get_udword(&rptr);
	db->uniqueIDseed = get_udword(&rptr);

/* XXX - These printf() statements should be controlled by a
 * debugging flag.
 */
#if 0
printf("\tname: \"%s\"\n", db->name);
printf("\tattributes: 0x%04x", db->attributes);
if (db->attributes & PDB_ATTR_RESDB) printf(" RESDB");
if (db->attributes & PDB_ATTR_RO) printf(" RO");
if (db->attributes & PDB_ATTR_APPINFODIRTY)
	 printf(" APPINFODIRTY");
if (db->attributes & PDB_ATTR_BACKUP) printf(" BACKUP");
if (db->attributes & PDB_ATTR_OKNEWER) printf(" OKNEWER");
if (db->attributes & PDB_ATTR_RESET) printf(" RESET");
if (db->attributes & PDB_ATTR_OPEN) printf(" OPEN");
printf("\n");
printf("\tversion: %u\n", db->version);
t = db->ctime - EPOCH_1904;
printf("\tctime: %lu %s", db->ctime,
	ctime(&t));
t = db->mtime - EPOCH_1904;
printf("\tmtime: %lu %s", db->mtime,
	ctime(&t));
t = db->baktime - EPOCH_1904;
printf("\tbaktime: %lu %s", db->baktime,
	ctime(&t));
printf("\tmodnum: %ld\n", db->modnum);
printf("\tappinfo_offset: 0x%08lx\n",
	db->appinfo_offset);
printf("\tsortinfo_offset: 0x%08lx\n",
	db->sortinfo_offset);
printf("\ttype: '%c%c%c%c' (0x%08lx)\n",
	(char) (db->type >> 24) & 0xff,
	(char) (db->type >> 16) & 0xff,
	(char) (db->type >> 8) & 0xff,
	(char) db->type & 0xff,
	db->type);
printf("\tcreator: '%c%c%c%c' (0x%08lx)\n",
	(char) (db->creator >> 24) & 0xff,
	(char) (db->creator >> 16) & 0xff,
	(char) (db->creator >> 8) & 0xff,
	(char) db->creator & 0xff,
	db->creator);
printf("\tuniqueIDseed: %ld\n", db->uniqueIDseed);
#endif	/* 0 */

	return 0;		/* Success */
}

/* pdb_LoadRecListHeader
 * Load the record list header from a pdb file, and fill in the appropriate
 * fields in 'db'.
 */
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
	db->next_reclistID = get_udword(&rptr);
	db->numrecs = get_uword(&rptr);

/* XXX - These printf() statements should be controlled by a
 * debugging flag.
 */
/*  printf("\tnextID: %ld\n", db->next_reclistID); */
/*  printf("\tlen: %u\n", db->numrecs); */

	return 0;
}

/* pdb_LoadRsrcIndex
 * Read the resource index from a resource database file, and fill in the
 * appropriate fields in 'db'.
 */
static int
pdb_LoadRsrcIndex(int fd,
		  struct pdb *db)
{
	int i;
	int err;

	if (db->numrecs == 0)
	{
		/* There are no resources in this file */
		db->rec_index.res = NULL;
		return 0;
	}

	/* Read the resource index */
	for (i = 0; i < db->numrecs; i++)
	{
		static ubyte inbuf[PDB_RESOURCEIX_LEN];
					/* Input buffer */
		const ubyte *rptr;	/* Pointer into buffers, for reading */
		struct pdb_resource *rsrc;
					/* New resource entry */

		/* Allocate the resource entry */
		if ((rsrc = (struct pdb_resource *)
		     malloc(sizeof(struct pdb_resource)))
		    == NULL)
			return -1;
		/* Scribble zeros all over it, just in case */
		memset(rsrc, 0, sizeof(struct pdb_resource));

		/* Read the next resource index entry */
		if ((err = read(fd, inbuf, PDB_RESOURCEIX_LEN)) !=
		    PDB_RESOURCEIX_LEN)
			return -1;

		/* Parse it */
		rptr = inbuf;
		rsrc->type = get_udword(&rptr);
		rsrc->id = get_uword(&rptr);
		rsrc->offset = get_udword(&rptr);

/* XXX - These printf() statements should be controlled by a
 * debugging flag.
 */
#if 0
printf("\tResource %d: type '%c%c%c%c' (0x%08lx), id %u, offset 0x%04lx\n",
       i,
       (char) (rsrc->type >> 24) & 0xff,
       (char) (rsrc->type >> 16) & 0xff,
       (char) (rsrc->type >> 8) & 0xff,
       (char) rsrc->type & 0xff,
       rsrc->type,
       rsrc->id,
       rsrc->offset);
#endif	/* 0 */ 

		/* Append the new resource to the list */
		pdb_AppendResource(db, rsrc);
	}

	return 0;
}

/* pdb_LoadRecIndex
 * Read the record index from a record database file, and fill in the
 * appropriate fields in 'db'.
 */
static int
pdb_LoadRecIndex(int fd,
		 struct pdb *db)
{
	int i;
	int err;

	if (db->numrecs == 0)
	{
		/* There are no records in this file */
		db->rec_index.rec = NULL;
		return 0;
	}

	/* Read the record index */
	for (i = 0; i < db->numrecs; i++)
	{
		static ubyte inbuf[PDB_RECORDIX_LEN];
					/* Input buffer */
		const ubyte *rptr;	/* Pointer into buffers, for reading */
		struct pdb_record *rec;
					/* New record entry */

		/* Allocate the record entry */
		if ((rec = (struct pdb_record *)
		     malloc(sizeof(struct pdb_record)))
		    == NULL)
			return -1;
		/* Scribble zeros all over it, just in case */
		memset(rec, 0, sizeof(struct pdb_record));

		/* Read the next record index entry */
		if ((err = read(fd, inbuf, PDB_RECORDIX_LEN)) !=
		    PDB_RECORDIX_LEN)
			return -1;

		/* Parse it */
		rptr = inbuf;
		rec->offset = get_udword(&rptr);
		rec->attributes = get_ubyte(&rptr);
		rec->uniqueID =
			((udword) (get_ubyte(&rptr) << 16)) |
			((udword) (get_ubyte(&rptr) << 8)) |
			((udword) get_ubyte(&rptr));

/* XXX - These printf() statements should be controlled by a
 * debugging flag.
 */
#if 0
printf("\tRecord %d: offset 0x%04lx, attr 0x%02x, uniqueID 0x%08lx\n",
       i,
       rec->offset,
       rec->attributes,
       rec->uniqueID);
#endif	/* 0 */

		/* Append the new record to the database */
		pdb_AppendRecord(db, rec); 
	}

	return 0;
}

/* pdb_LoadAppBlock
 * Read the AppInfo block from a database file, and fill in the appropriate
 * fields in 'db'. If the file doesn't have an AppInfo block, set it to
 * NULL.
 */
static int
pdb_LoadAppBlock(int fd,
		 struct pdb *db)
{
	int err;
	uword next_off;		/* Offset of the next thing in the file
				 * after the AppInfo block */
	off_t offset;		/* Offset into file, for checking */

	/* Check to see if there even *is* an AppInfo block */
	if (db->appinfo_offset == 0L)
	{
		/* Nope */
		db->appinfo_len = 0L;
		db->appinfo = NULL;
		return 0;
	}

	/* Figure out how long the AppInfo block is, by comparing its
	 * offset to that of the next thing in the file.
	 */
	if (db->sortinfo_offset > 0L)
		/* There's a sort block */
		next_off = db->sortinfo_offset;
	else if (db->numrecs > 0)
	{
		/* There's no sort block, but there are records. Get the
		 * offset of the first one.
		 */
		if (IS_RSRC_DB(db))
			next_off = db->rec_index.res->offset;
		else
			next_off = db->rec_index.rec->offset;
	} else
		/* There is neither sort block nor records, so the AppInfo
		 * block must go to the end of the file.
		 */
		next_off = db->file_size;

	/* Subtract the AppInfo block's offset from that of the next thing
	 * in the file to get the AppInfo block's length.
	 */
	db->appinfo_len = next_off - db->appinfo_offset;

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
	if (offset != db->appinfo_offset)
	{
		/* Oops! We're in the wrong place */
		fprintf(stderr, "Warning: AppInfo block isn't where I thought it would be.\n"
			"expected 0x%lx, but we're at 0x%qx\n",
			db->appinfo_offset, offset);

		/* Try to recover */
		offset = lseek(fd, db->appinfo_offset, SEEK_SET);
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

/* pdb_LoadSortBlock
 * Read the sort block from a database file, and fill in the appropriate
 * fields in 'db'. If the file doesn't have a sort block, set it to NULL.
 *
 * XXX - Largely untested, since not that many databases have sort blocks.
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
	if (db->sortinfo_offset == 0L)
	{
		/* Nope */
		db->sortinfo_len = 0L;
		db->sortinfo = NULL;
		return 0;
	}

	/* Figure out how long the sort block is, by comparing its
	 * offset to that of the next thing in the file.
	 */
	if (db->numrecs > 0)
	{
		/* There are records. Get the offset of the first one.
		 */
		if (IS_RSRC_DB(db))
			next_off = db->rec_index.res->offset;
		else
			next_off = db->rec_index.rec->offset;
	} else
		/* There are no records, so the sort block must go to the
		 * end of the file.
		 */
		next_off = db->file_size;

	/* Subtract the sort block's offset from that of the next thing
	 * in the file to get the sort block's length.
	 */
	db->sortinfo_len = next_off - db->sortinfo_offset;

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
	if (offset != db->sortinfo_offset)
	{
		/* Oops! We're in the wrong place */
		fprintf(stderr, "Warning: sort block isn't where I thought it would be.\n"
			"Expected 0x%lx, but we're at 0x%qx\n",
			db->sortinfo_offset, offset);

		/* Try to recover */
		offset = lseek(fd, db->sortinfo_offset, SEEK_SET);
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

/* pdb_LoadResources
 * Read each resource in turn from a resource database file.
 */
static int
pdb_LoadResources(int fd,
		  struct pdb *db)
{
	int i;
	int err;
	struct pdb_resource *rsrc;

	/* This assumes that the resource list has already been created by
	 * 'pdb_LoadRsrcIndex()'.
	 */
	for (i = 0, rsrc = db->rec_index.res;
	     i < db->numrecs;
	     i++, rsrc = rsrc->next)
	{
		off_t offset;		/* Current offset, for checking */
		udword next_off;	/* Offset of next resource in file */

		/* Sanity check: make sure we haven't stepped off the end
		 * of the list.
		 */
		if (rsrc == NULL)
		{
			fprintf(stderr, "Hey! I can't find the %dth resource!\n",
				i);
			return -1;
		}

printf("Reading resource %d (type '%c%c%c%c')\n",
       i,
       (char) (rsrc->type >> 24) & 0xff,
       (char) (rsrc->type >> 16) & 0xff,
       (char) (rsrc->type >> 8) & 0xff,
       (char) rsrc->type & 0xff);

		/* Out of paranoia, make sure we're in the right place */
		offset = lseek(fd, 0, SEEK_CUR);
					/* Find out where we are now */
		if (offset != rsrc->offset)
		{
			fprintf(stderr, "Warning: resource %d isn't where I thought it would be.\n"
				"Expected 0x%lx, but we're at 0x%qx\n",
				i,
				rsrc->offset, offset);
			/* Try to recover */
			offset = lseek(fd, rsrc->offset, SEEK_SET);
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

		/* Okay, now that we're in the right place, find out what
		 * the next thing in the file is: its offset will tell us
		 * how much to read.
		 * It's debatable whether 'i' or 'rsrc' should be
		 * authoritative for determining the offset of the next
		 * resource. I'm going to choose 'rsrc', since I think
		 * that's more likely to be immune to fencepost errors. The
		 * two should, however, be equivalent. In fact, it might be
		 * a Good Thing to add a check to make sure.
		 */
		if (rsrc->next == NULL)
		{
			/* This is the last resource in the file, so it
			 * goes to the end of the file.
			 */
			next_off = db->file_size;
		} else {
			/* This isn't the last resource. Find the next
			 * one's offset.
			 */
			next_off = rsrc->next->offset;
		}

		/* Subtract this resource's index from that of the next
		 * thing, to get the size of this resource.
		 */
		rsrc->data_len = next_off - rsrc->offset;

		/* Allocate space for this resource */
		if ((rsrc->data = (ubyte *) malloc(rsrc->data_len)) == NULL)
		{
			fprintf(stderr, "Out of memory\n");
			return -1;
		}

		/* Read the resource */
		if ((err = read(fd, rsrc->data, rsrc->data_len)) !=
		    rsrc->data_len)
		{
			fprintf(stderr, "Can't read resource %d\n", i);
			perror("pdb_LoadResources: read");
			return -1;
		}
/*  printf("Contents of resource %d:\n", i); */
/*  debug_dump(stdout, "<RSRC", rsrc->data, rsrc->data_len); */
	}

	return 0;		/* Success */
}

/* pdb_LoadRecords
 * Read each record in turn from a record database file.
 */
static int
pdb_LoadRecords(int fd,
		struct pdb *db)
{
	int i;
	int err;
	struct pdb_record *rec;

	/* This assumes that the record list has already been created by
	 * 'pdb_LoadRecIndex()'.
	 */
	for (i = 0, rec = db->rec_index.rec;
	     i < db->numrecs;
	     i++, rec = rec->next)
	{
		off_t offset;		/* Current offset, for checking */
		udword next_off;	/* Offset of next resource in file */

		/* Sanity check: make sure we haven't stepped off the end
		 * of the list.
		 */
		if (rec == NULL)
		{
			fprintf(stderr, "Hey! I can't find the %dth record!\n",
				i);
			return -1;
		}

printf("Reading record %d (id 0x%08lx)\n", i, rec->uniqueID);

		/* Out of paranoia, make sure we're in the right place */
		offset = lseek(fd, 0, SEEK_CUR);
					/* Find out where we are now */
		if (offset != rec->offset)
		{
			fprintf(stderr, "Warning: record %d isn't where I thought it would be.\n"
				"Expected 0x%lx, but we're at 0x%qx\n",
				i,
				rec->offset, offset);
			/* Try to recover */
			offset = lseek(fd, rec->offset, SEEK_SET);
						/* Go to where this record
						 * ought to be. */
			if (offset < 0)
			{
				/* Something's wrong */
				fprintf(stderr, "Can't find record %d\n",
					i);
				return -1;
			}
		}

		/* Okay, now that we're in the right place, find out what
		 * the next thing in the file is: its offset will tell us
		 * how much to read.
		 * It's debatable whether 'i' or 'rec' should be
		 * authoritative for determining the offset of the next
		 * resource. I'm going to choose 'rec', since I think
		 * that's more likely to be immune to fencepost errors. The
		 * two should, however, be equivalent. In fact, it might be
		 * a Good Thing to add a check to make sure.
		 */
		if (rec->next == NULL)
		{
			/* This is the last record in the file, so it goes
			 * to the end of the file.
			 */
			next_off = db->file_size;
		} else {
			/* This isn't the last record. Find the next one's
			 * offset.
			 */
			next_off = rec->next->offset;
		}

		/* Subtract this record's index from that of the next one,
		 * to get the size of this record.
		 */
		rec->data_len = next_off - rec->offset;

		/* Allocate space for this record */
		if ((rec->data = (ubyte *) malloc(rec->data_len)) == NULL)
		{
			fprintf(stderr, "Out of memory\n");
			return -1;
		}

		/* Read the record */
		if ((err = read(fd, rec->data, rec->data_len)) !=
		    rec->data_len)
		{
			fprintf(stderr, "Can't read record %d\n", i);
			perror("pdb_LoadRecords: read");
			return -1;
		}
/*  printf("Contents of record %d:\n", i); */
/*  debug_dump(stdout, "<REC", rec->data, rec->data_len); */
	}

	return 0;		/* Success */
}

/* pdb_DownloadResources
 * Download a resource database's resources from the Palm, and put them in
 * 'db'.
 */
static int
pdb_DownloadResources(struct PConnection *pconn,
		      ubyte dbh,
		      struct pdb *db)
{
	int i;
	int err;

	/* Read each resource in turn */
	for (i = 0; i < db->numrecs; i++)
	{
		struct pdb_resource *rsrc;	/* The new resource */
		struct dlp_resource resinfo;	/* Resource info will be
						 * read into here before
						 * being parsed into
						 * 'rsrc'.
						 */
		const ubyte *rptr;	/* Pointer into buffers, for reading */


		/* Allocate the new resource */
		if ((rsrc = (struct pdb_resource *)
		     malloc(sizeof(struct pdb_resource)))
		    == NULL)
		{
			fprintf(stderr, "pdb_DownloadResources: out of memory\n");
			return -1;
		}

		/* Download the 'i'th resource from the Palm */
		err = DlpReadResourceByIndex(pconn, dbh, i, 0,
					     DLPC_RESOURCE_TOEND,
					     &resinfo,
					     &rptr);

		if (err != DLPSTAT_NOERR)
		{
			fprintf(stderr, "Can't read resource %d: %d\n",
				i, err);
			return -1;
		}

fprintf(stderr, "DLP resource data %d:\n", i);
fprintf(stderr, "\ttype: '%c%c%c%c'\n",
	(char) ((resinfo.type >> 24) & 0xff),
	(char) ((resinfo.type >> 16) & 0xff),
	(char) ((resinfo.type >> 8) & 0xff),
	(char) (resinfo.type & 0xff));
fprintf(stderr, "\tid: %d\n", resinfo.id);
fprintf(stderr, "\tindex: %d\n", resinfo.index);
fprintf(stderr, "\tsize: %d\n", resinfo.size);

		/* Fill in the resource index data */
		rsrc->type = resinfo.type;
		rsrc->id = resinfo.id;
		rsrc->offset = 0L;	/* For now */
					/* XXX - Should this be filled in? */

		/* Fill in the data size entry */
		rsrc->data_len = resinfo.size;

		/* Allocate space in 'rsrc' for the resource data itself */
		if ((rsrc->data = (ubyte *) malloc(rsrc->data_len)) == NULL)
		{
			fprintf(stderr, "pdb_DownloadResources: out of memory.\n");
			return -1;
		}

		/* Copy the resource data to 'rsrc' */
		memcpy(rsrc->data, rptr, rsrc->data_len);
/*  debug_dump(stderr, "RES", rsrc->data, rsrc->data_len); */

		/* Append the resource to the database */
		pdb_AppendResource(db, rsrc);
	}

	return 0;	/* Success */
}

static int
pdb_DownloadRecords(struct PConnection *pconn,
		   ubyte dbh,
		   struct pdb *db)
{
	int i;
	int err;
	udword *recids;		/* Array of record IDs */
	uword numrecs;		/* # record IDs actually read */

	/* Handle the easy case first: if there aren't any records, don't
	 * bother asking for their IDs.
	 */
	if (db->numrecs == 0)
	{
		/* No records */
		db->rec_index.rec = NULL;

		return 0;
	}

	/* Allocate the array of record IDs.
	 * This is somewhat brain-damaged: ideally, we'd like to just read
	 * each record in turn. It'd seem that DlpReadRecordByIndex() would
	 * be just the thing; but it doesn't actually return the record
	 * data, it just returns record info. So instead, we have to use
	 * DlpReadRecordIDList() to get a list with each record's ID, then
	 * use DlpReadRecordByID() to get the record data.
	 */
	if ((recids = (udword *) calloc(db->numrecs,
					sizeof(udword)))
	    == NULL)
	{
		fprintf(stderr, "Can't allocate list of record IDs\n");
		return -1;
	}

	/* Get the list of record IDs, as described above */
	if ((err = DlpReadRecordIDList(pconn, dbh, 0,
				       0, DLPC_RRECIDL_TOEND,
				       &numrecs, recids))
	    != DLPSTAT_NOERR)
	{
		fprintf(stderr, "Can't read record ID list\n");
		free(recids);
		return -1;
	}

	/* Sanity check */
	if (numrecs != db->numrecs)
	{
		fprintf(stderr, "### Whoa! numrecs is %d, but ReadRecordIDList says %d!\n",
			db->numrecs, numrecs);
		/* XXX - What to do in this case? For now, just punt. */
		free(recids);
		return -1;
	}

	/* Read each record in turn */
	for (i = 0; i < db->numrecs; i++)
	{
		struct pdb_record *rec;		/* The new resource */
		struct dlp_recinfo recinfo;	/* Record info will be read
						 * into here before being
						 * parsed into 'rec'.
						 */
		const ubyte *rptr;	/* Pointer into buffers, for reading */


		/* Allocate the new record */
		if ((rec = (struct pdb_record *)
		     malloc(sizeof(struct pdb_record)))
		    == NULL)
		{
			fprintf(stderr, "pdb_DownloadRecords: out of memory\n");
			free(recids);
			return -1;
		}

		/* Download the 'i'th record from the Palm */
		err = DlpReadRecordByID(pconn, dbh,
					recids[i],
					0, DLPC_RECORD_TOEND,
					&recinfo,
					&rptr);
		if (err != DLPSTAT_NOERR)
		{
			fprintf(stderr, "Can't read record %d: %d\n",
				i, err);
			free(recids);
			return -1;
		}

fprintf(stderr, "DLP record data %d:\n", i);
fprintf(stderr, "\tid: 0x%08lx\n", recinfo.id);
fprintf(stderr, "\tindex: %d\n", recinfo.index);
fprintf(stderr, "\tsize: %d\n", recinfo.size);
fprintf(stderr, "\tattributes: 0x%02x\n", recinfo.attributes);
fprintf(stderr, "\tcategory: %d\n", recinfo.category); 

		/* Fill in the record index data */
		rec->offset = 0L;	/* For now */
					/* XXX - Should this be filled in? */
		rec->attributes = recinfo.attributes;
		rec->uniqueID = recinfo.id;

		/* Fill in the data size entry */
		rec->data_len = recinfo.size;

		/* Allocate space in 'rec' for the record data itself */
		if ((rec->data = (ubyte *) malloc(rec->data_len)) == NULL)
		{
			fprintf(stderr, "pdb_DownloadRecords: out of memory.\n");
			free(recids);
			return -1;
		}

		/* Copy the record data to 'rec' */
		memcpy(rec->data, rptr, rec->data_len);
/*  debug_dump(stderr, "REC", rec->data, rec->data_len); */

		/* Append the record to the database */
		pdb_AppendRecord(db, rec);
	}

	return 0;	/* Success */
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
