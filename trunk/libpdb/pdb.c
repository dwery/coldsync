/* pdb.c
 *
 * Functions for dealing with Palm databases and such.
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: pdb.c,v 1.3 1999-11-02 03:50:45 arensb Exp $
 */

#include "config.h"
#include <stdio.h>
#include <fcntl.h>		/* For open() */
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/param.h>		/* For MAXPATHLEN */
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>		/* For strncat() et al. */
/*  #include "coldsync.h" */
/*#include "palm_types.h"
#include "util.h"*/
#include <pconn/pconn.h>	/* XXX - Clean this up */
#include "pdb.h"

#define PDB_TRACE(n)	if (0)	/* XXX - Figure out how best to put this
				 * back */

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

/* pdb_FreeRecord
 * Free a previously-allocated 'pdb_record'. This function wouldn't really
 * be necessary, except that pdb_CopyRecord() returns a 'pdb_record'.
 */
void
pdb_FreeRecord(struct pdb_record *rec)
{
	if (rec->data != NULL)
		free(rec->data);
	free(rec);
}

/* pdb_FreeResource
 * Free a previously-allocated 'pdb_resource'. This function wouldn't
 * really be necessary, except that pdb_CopyResource() returns a
 * 'pdb_resource'.
 */
void
pdb_FreeResource(struct pdb_resource *rsrc)
{
	if (rsrc->data != NULL)
		free(rsrc->data);
	free(rsrc);
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
		struct pdb_resource *rsrc;
		struct pdb_resource *next;

		/* Walk the linked list, freeing as we go along */
		for (rsrc = db->rec_index.rsrc;
		     rsrc != NULL;
		     rsrc = next)
		{
			next = rsrc->next;	/* Remember the next
						 * element on the list. We
						 * won't have a chance to
						 * look it up after this
						 * one has been free()d.
						 */

			/* Free this element */
			pdb_FreeResource(rsrc);
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

			/* Free this element */
			pdb_FreeRecord(rec);
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
 * relevant characteristics, like creator, type, and modification number)
 * without having to load their entire contents.
 * OTOH, if the desktop machine has infinite memory and CPU, it might be
 * reasonable to assume that it can keep everything in memory with
 * negligible overhead.
 */

/* pdb_Read
 * Read a PDB from the file descriptor 'fd'. This must already have been
 * opened for reading and/or writing.
 *
 * Note: this function does not to any locking. The caller is responsible
 * for that.
 */
struct pdb *
pdb_Read(int fd)
{
	int err;
	struct pdb *retval;
	static ubyte useless_buf[2];	/* Buffer for the useless 2 bytes
					 * after the record index.
					 */

	/* Create a new pdb to return */
	if ((retval = new_pdb()) == NULL)
	{
		return NULL;
	}

	/* Find out how long the file is */
	retval->file_size = get_file_length(fd);

	/* Load the header */
	if ((err = pdb_LoadHeader(fd, retval)) < 0)
	{
		fprintf(stderr, "Can't load header\n");
		free_pdb(retval);
		return NULL;
	}

	/* Load the record list header */
	if ((err = pdb_LoadRecListHeader(fd, retval)) < 0)
	{
		fprintf(stderr, "Can't load header\n");
		free_pdb(retval);
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
			return NULL;
		}
	} else {
		/* Read the record index */
		if ((err = pdb_LoadRecIndex(fd, retval)) < 0)
		{
			fprintf(stderr, "Can't read record index\n");
			free_pdb(retval);
			return NULL;
		}
	}

	/* Skip the dummy two bytes */
	if ((err = read(fd, useless_buf, 2)) != 2)
	{
		fprintf(stderr, "Can't read the useless two bytes");
		perror("LoadDatabase: read");
		free_pdb(retval);
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
		return NULL;
	}

	/* Load the sort block, if any */
	if ((err = pdb_LoadSortBlock(fd, retval)) < 0)
	{
		fprintf(stderr, "Can't read sort block\n");
		free_pdb(retval);
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
			return NULL;
		}
	} else {
		/* Read the records */
		if ((err = pdb_LoadRecords(fd, retval)) < 0)
		{
			fprintf(stderr, "Can't read records.\n");
			free_pdb(retval);
			return NULL;
		}
	}

	return retval;			/* Success */
}

/* pdb_Write
 * Write 'db' to the file descriptor 'fd'. This must already have been
 * opened for writing.
 *
 * Note that while you can open the backup file for reading and writing,
 * read from it with pdb_Read() and save it with pdb_Write(), this is not
 * recommended: if anything should go wrong at the wrong time (e.g., the
 * disk fills up just as you're about to write the database back to disk),
 * you will lose the entire backup.
 * A better approach is to use a staging file: read from the backup file,
 * write to a temporary file, then use rename() to move the temporary file
 * onto the real one. Alternately, you can copy the original file to a
 * temporary one, then open the temporary for both reading and writing.
 * This might have some advantages, in that it allows you to lock a single
 * file for the duration of the sync.
 *
 * Note: this function does not lock the file. The caller is responsible
 * for that.
 */
int
pdb_Write(const struct pdb *db,
	  int fd)
{
	static ubyte header_buf[PDB_HEADER_LEN];
				/* Buffer for writing database header */
	static ubyte rlheader_buf[PDB_RECORDLIST_LEN];
				/* Buffer for writing the record list header */
	static ubyte nul_buf[2];
				/* Buffer for writing the two useless NULs */
	ubyte *wptr;		/* Pointer into buffers, for writing */
	udword offset;		/* The next offset we're interested in */

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
		return -1;
	}

	/* Write the record/resource index */
	if (IS_RSRC_DB(db))
	{
		/* It's a resource database */
		struct pdb_resource *rsrc;	/* Current resource */

		/* Go through the list of resources, writing each one */
		for (rsrc = db->rec_index.rsrc;
		     rsrc != NULL;
		     rsrc = rsrc->next)
		{
			static ubyte rsrcbuf[PDB_RESOURCEIX_LEN];
					/* Buffer to hold the resource
					 * index entry.
					 */

			/* Construct the resource index entry */
			wptr = rsrcbuf;
			put_udword(&wptr, rsrc->type);
			put_uword(&wptr, rsrc->id);
			put_udword(&wptr, offset);

			/* Write the resource index entry */
			if (write(fd, rsrcbuf, PDB_RESOURCEIX_LEN) !=
			    PDB_RESOURCEIX_LEN)
			{
				fprintf(stderr, "pdb_Write: "
					"Can't write resource index entry\n");
				perror("write");
				return -1;
			}

			/* Bump 'offset' up to point to the offset of the
			 * next variable-sized thing in the file.
			 */
			offset += rsrc->data_len;
		}
	} else {
		/* It's a record database */
		struct pdb_record *rec;		/* Current record */

		/* Go through the list of records, writing each one */
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
			put_ubyte(&wptr, (char) ((rec->id >> 16) & 0xff));
			put_ubyte(&wptr, (char) ((rec->id >> 8) & 0xff));
			put_ubyte(&wptr, (char) (rec->id & 0xff));

			/* Write the resource index entry */
			if (write(fd, recbuf, PDB_RECORDIX_LEN) !=
			    PDB_RECORDIX_LEN)
			{
				fprintf(stderr, "pdb_Write: "
					"Can't write RECORD index entry\n");
				perror("write");
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
		fprintf(stderr, "pdb_Write: "
			"Can't write the two useless NULs\n");
		perror("write");
		return -1;
	}

	/* Write the AppInfo block, if any */
	if (db->appinfo != NULL)
	{
		if (write(fd, db->appinfo, db->appinfo_len) !=
		    db->appinfo_len)
		{
			fprintf(stderr, "pdb_Write: "
				"Can't write AppInfo block\n");
			perror("write");
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
			return -1;
		}
	}

	/* Write the record/resource data */
	if (IS_RSRC_DB(db))
	{
		/* It's a resource database */
		struct pdb_resource *rsrc;

		/* Go through the list of resources, writing each one's
		 * data.
		 */
		for (rsrc = db->rec_index.rsrc;
		     rsrc != NULL;
		     rsrc = rsrc->next)
		{
			/* Write the data */
			if (write(fd, rsrc->data, rsrc->data_len) !=
			    rsrc->data_len)
			{
				fprintf(stderr, "pdb_Write: "
					"Can't write resource data\n");
				perror("write");
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
				fprintf(stderr, "pdb_Write: "
					"Can't write record data\n");
				perror("write");
				return -1;
			}
		}
	}

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
	PDB_TRACE(4)
	{
		fprintf(stderr, "pdb_Download:\n");
		fprintf(stderr, "\tname: \"%s\"\n", retval->name);
		fprintf(stderr, "\tattributes: 0x%04x\n", retval->attributes);
		fprintf(stderr, "\tversion: %d\n", retval->version);
		fprintf(stderr, "\tctime: %ld\n", retval->ctime);
		fprintf(stderr, "\tmtime: %ld\n", retval->mtime);
		fprintf(stderr, "\tbaktime: %ld\n", retval->baktime);
		fprintf(stderr, "\tmodnum: %ld\n", retval->modnum);
		fprintf(stderr, "\tappinfo_offset: %ld\n",
			retval->appinfo_offset);
		fprintf(stderr, "\tsortinfo_offset: %ld\n",
			retval->sortinfo_offset);
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
	}

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
	PDB_TRACE(4)
	{
		fprintf(stderr, "\n\tnextID: %ld\n", retval->next_reclistID);
		fprintf(stderr, "\tlen: %d\n", retval->numrecs);
	}

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
		PDB_TRACE(4)
			fprintf(stderr,
				"pdb_Download: got an AppInfo block\n");
		PDB_TRACE(6)
			debug_dump(stderr, "APP", retval->appinfo,
				   retval->appinfo_len);
		break;
	    case DLPSTAT_NOTFOUND:
		/* This database doesn't have an AppInfo block */
		retval->appinfo_len = 0;
		retval->appinfo = NULL;
		PDB_TRACE(5)
			fprintf(stderr, "pdb_Download: this db doesn't have "
				"an AppInfo block\n");
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
	PDB_TRACE(7)
		fprintf(stderr,
			"After pdb_Download{Resources,Records}; err == %d\n",
			err);
	if (err < 0)
	{
		fprintf(stderr, "Can't download record or resource index\n");
		DlpCloseDB(pconn, dbh);
		free_pdb(retval);
		return NULL;
	}

	return retval;			/* Success */
}

/* pdb_Upload
 * Upload 'db' to the Palm. This database must not exist (i.e., it's the
 * caller's responsibility to delete it if necessary).
 * When a record is uploaded, the Palm may assign it a new record number.
 * pdb_Upload() records this change in 'db', but it is the caller's
 * responsibility to save this change to the appropriate file, if
 * applicable.
 */
int
pdb_Upload(struct PConnection *pconn,
	   struct pdb *db)
{
	int err;
	ubyte dbh;			/* Database handle */
	struct dlp_createdbreq newdb;	/* Argument for creating a new
					 * database */

	PDB_TRACE(1)
		fprintf(stderr, "Uploading \"%s\"\n", db->name);

	/* Call OpenConduit to let the Palm (or the user) know that
	 * something's going on. (Actually, I don't know that that's the
	 * reason. I'm just imitating HotSync, here.
	 */
	err = DlpOpenConduit(pconn);
	if (err != DLPSTAT_NOERR)
	{
		fprintf(stderr, "Can't open conduit for \"%s\", err == %d\n",
			db->name, err);
		return -1;
	}

	/* Create the database */
	newdb.creator = db->creator;
	newdb.type = db->type;
	newdb.card = CARD0;
	newdb.flags = db->attributes;
			/* XXX - Is this right? This is voodoo code */
	newdb.version = db->version;
	memcpy(newdb.name, db->name, PDB_DBNAMELEN);

	err = DlpCreateDB(pconn, &newdb, &dbh);
	if (err != DLPSTAT_NOERR)
	{
		fprintf(stderr, "Error creating database \"%s\", err == %d\n",
			db->name, err);
		return -1;
	}

	/* Upload the AppInfo block, if it exists */
	if (db->appinfo_len > 0)
	{
		PDB_TRACE(3)
			fprintf(stderr, "Uploading AppInfo block\n");

		err = DlpWriteAppBlock(pconn, dbh,
				       0, db->appinfo_len,
				       db->appinfo);
		if (err < 0)
			return err;
	}

	/* Upload the sort block, if it exists */
	if (db->sortinfo_len > 0)
	{
		PDB_TRACE(3)
			fprintf(stderr, "Uploading sort block\n");

		err = DlpWriteSortBlock(pconn, dbh,
					0, db->sortinfo_len,
					db->sortinfo);
		if (err < 0)
			return err;
	}

	/* Upload each record/resource in turn */
	if (IS_RSRC_DB(db))
	{
		/* It's a resource database */
		struct pdb_resource *rsrc;

		PDB_TRACE(4)
			fprintf(stderr, "Uploading resources.\n");

		for (rsrc = db->rec_index.rsrc;
		     rsrc != NULL;
		     rsrc = rsrc->next)
		{
			PDB_TRACE(5)
				fprintf(stderr,
					"Uploading resource 0x%04x\n",
					rsrc->id);

			err = DlpWriteResource(pconn,
					       dbh,
					       rsrc->type,
					       rsrc->id,
					       rsrc->data_len,
					       rsrc->data);

			if (err != DLPSTAT_NOERR)
			{
				/* Close the database */
				err = DlpCloseDB(pconn, dbh);
				return -1;
			}
		}
	} else {
		/* It's a record database */
		struct pdb_record *rec;

		PDB_TRACE(4)
			fprintf(stderr, "Uploading records.\n");

		for (rec = db->rec_index.rec;
		     rec != NULL;
		     rec = rec->next)
		{
			udword newid;		/* New record ID */
			PDB_TRACE(5)
				fprintf(stderr,
					"Uploading record 0x%08lx\n",
					rec->id);

			err = DlpWriteRecord(pconn,
					     dbh,
					     0x80,	/* Mandatory magic */
							/* XXX - Actually,
							 * at some point
							 * DlpWriteRecord
							 * will get fixed
							 * to make sure the
							 * high bit is set,
							 * at which point
							 * this argument be
							 * allowed to be 0.
							 */
					     rec->id,
					     (rec->attributes & 0xf0),
					     (rec->attributes & 0x0f),
					     rec->data_len,
					     rec->data,
					     &newid);

			if (err != DLPSTAT_NOERR)
			{
				/* Close the database */
				err = DlpCloseDB(pconn, dbh);
				return -1;
			}

			/* Update the ID assigned to this record */
			rec->id = newid;
		}
	}

	/* Clean up */
	err = DlpCloseDB(pconn, dbh);

	return 0;		/* Success */
}

/* pdb_FindRecordByID
 * Find the record in 'db' whose ID is 'id'. Return a pointer to it. If no
 * such record exists, or in case of error, returns NULL.
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
		if (rec->id == id)
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
		/* See if the ID matches */
		if (rec->id == id)
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

		db->numrecs++;		/* Bump record counter */

		return 0;		/* Success */
	}

	/* Walk the list to find its end */
	for (rec = db->rec_index.rec; rec->next != NULL; rec = rec->next)
		;
	rec->next = newrec;
	newrec->next = NULL;

	db->numrecs++;			/* Bump record counter */

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
	if (db->rec_index.rsrc == NULL)
	{
		/* XXX - Sanity check: db->numrecs should be 0 */
		db->rec_index.rsrc = newrsrc;
		newrsrc->next = NULL;

		db->numrecs++;		/* Bump resource counter */

		return 0;		/* Success */
	}

	/* Walk the list to find its end */
	for (rsrc = db->rec_index.rsrc; rsrc->next != NULL; rsrc = rsrc->next)
		;
	rsrc->next = newrsrc;
	newrsrc->next = NULL;

	db->numrecs++;			/* Bump resource counter */

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
		newrsrc->next = db->rec_index.rsrc;
		db->rec_index.rsrc = newrsrc;
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

/* new_Record
 * Create a new record from the given arguments, and return a pointer to
 * it. Returns NULL in case of error.
 * The record data is copied, so the caller needs to take care of freeing
 * 'data'.
 */
struct pdb_record *
new_Record(const ubyte attributes,
	   const udword id,
	   const uword len,
	   const ubyte *data)
{
	struct pdb_record *retval;

	/* Allocate the record to be returned */
	if ((retval = (struct pdb_record *) malloc(sizeof(struct pdb_record)))
	    == NULL)
	{
		fprintf(stderr, "new_Record: out of memory\n");
		return NULL;
	}

	/* Initialize the new record */
	retval->next = NULL;
	retval->offset = 0L;
	retval->attributes = attributes;
	retval->id = id;

	/* Allocate space to put the record data */
	if ((retval->data = (ubyte *) malloc(len)) == NULL)
	{
		/* Couldn't allocate data portion of record */
		fprintf(stderr, "new_Record: can't allocate data\n");
		free(retval);
		return NULL;
	}

	/* Copy the data to the new record */
	retval->data_len = len;
	memcpy(retval->data, data, len);

	return retval;		/* Success */
}

/* XXX - new_Resource */

/* pdb_CopyRecord
 * Make a copy of record 'rec' in database 'db' (and its data), and return
 * it. The new record is allocated by pdb_CopyRecord(), so the caller has
 * to take care of freeing it.
 * Returns a pointer to the new copy, or NULL in case of error.
 */
struct pdb_record *pdb_CopyRecord(
	const struct pdb *db,
	const struct pdb_record *rec)
{
	struct pdb_record *retval;

	/* Allocate the record to be returned */
	if ((retval = (struct pdb_record *) malloc(sizeof(struct pdb_record)))
	    == NULL)
	{
		fprintf(stderr, "pdb_CopyRecord: out of memory.\n");
		return NULL;
	}

	retval->next = NULL;		/* For cleanliness */

	/* Copy the old record to the new copy */
	retval->offset = rec->offset;
	retval->attributes = rec->attributes;
	retval->id = rec->id;

	/* Allocate space for the record data itself */
	if ((retval->data = (ubyte *) malloc(rec->data_len)) == NULL)
	{
		fprintf(stderr, "pdb_CopyRecord: can't allocate record data.\n");
		free(retval);
		return NULL;
	}

	/* Copy the record data */
	retval->data_len = rec->data_len;
	memcpy(retval->data, rec->data, retval->data_len);

	return retval;		/* Success */
}

/* pdb_CopyResource
 * Make a copy of resource 'rsrc' in database 'db' (and its data), and
 * return it. The new record is allocated by pdb_CopyResource(), so the
 * caller has to take care of freeing it.
 * Returns a pointer to the new copy, or NULL in case of error.
 */
struct pdb_resource *pdb_CopyResource(
	const struct pdb *db,
	const struct pdb_resource *rsrc)
{
	struct pdb_resource *retval;

	/* Allocate the resource to be returned */
	if ((retval = (struct pdb_resource *)
	     malloc(sizeof(struct pdb_resource))) == NULL)
	{
		fprintf(stderr, "pdb_CopyResource: out of memory.\n");
		return NULL;
	}

	retval->next = NULL;		/* For cleanliness */

	/* Copy the old resource to the new copy */
	retval->type = rsrc->type;
	retval->id = rsrc->id;
	retval->offset = rsrc->offset;

	/* Allocate space for the record data itself */
	if ((retval->data = (ubyte *) malloc(rsrc->data_len)) == NULL)
	{
		fprintf(stderr,
			"pdb_CopyResource: can't allocate resource data.\n");
		free(retval);
		return NULL;
	}

	/* Copy the resource data */
	retval->data_len = rsrc->data_len;
	memcpy(retval->data, rsrc->data, retval->data_len);

	return retval;		/* Success */
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

	PDB_TRACE(5)
	{
		time_t t;

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
	}

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

	PDB_TRACE(6)
	{
		printf("\tnextID: %ld\n", db->next_reclistID);
		printf("\tlen: %u\n", db->numrecs);
	}

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
	uword totalrsrcs;	/* The real number of resources in the
				 * database.
				 */

	totalrsrcs = db->numrecs;	/* Get the number of resources in
					 * the database. It is necessary to
					 * remember this here because
					 * dlp_AppendResource() increments
					 * db->numrecs in the name of
					 * convenience.
					 */

	if (totalrsrcs == 0)
	{
		/* There are no resources in this file */
		db->rec_index.rsrc = NULL;
		return 0;
	}

	/* Read the resource index */
	for (i = 0; i < totalrsrcs; i++)
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

		PDB_TRACE(6)
		{
			printf("\tResource %d: type '%c%c%c%c' (0x%08lx), "
			       "id %u, offset 0x%04lx\n",
			       i,
			       (char) (rsrc->type >> 24) & 0xff,
			       (char) (rsrc->type >> 16) & 0xff,
			       (char) (rsrc->type >> 8) & 0xff,
			       (char) rsrc->type & 0xff,
			       rsrc->type,
			       rsrc->id,
			       rsrc->offset);
		}

		/* Append the new resource to the list */
		pdb_AppendResource(db, rsrc);
		db->numrecs = totalrsrcs;	/* Kludge */
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
	uword totalrecs;	/* The real number of records in the
				 * database.
				 */

	totalrecs = db->numrecs;	/* Get the number of records in the
					 * database. It is necessary to
					 * remember this here because
					 * dlp_AppendResource() increments
					 * db->numrecs in the name of
					 * convenience.
					 */

	if (totalrecs == 0)
	{
		/* There are no records in this file */
		db->rec_index.rec = NULL;
		return 0;
	}

	/* Read the record index */
	for (i = 0; i < totalrecs; i++)
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
		{
			fprintf(stderr, "Out of memory\n");
			return -1;
		}

		/* Scribble zeros all over it, just in case */
		memset(rec, 0, sizeof(struct pdb_record));

		/* Read the next record index entry */
		if ((err = read(fd, inbuf, PDB_RECORDIX_LEN)) !=
		    PDB_RECORDIX_LEN)
		{
			fprintf(stderr, "LoadRecIndex: error reading record index entry (%d bytes): %d\n",
				PDB_RECORDIX_LEN,
				err);
			perror("read");
			free(rec);
			return -1;
		}

		/* Parse it */
		rptr = inbuf;
		rec->offset = get_udword(&rptr);
		rec->attributes = get_ubyte(&rptr);
		rec->id =
			((udword) (get_ubyte(&rptr) << 16)) |
			((udword) (get_ubyte(&rptr) << 8)) |
			((udword) get_ubyte(&rptr));

		PDB_TRACE(6)
			printf("\tRecord %d: offset 0x%04lx, attr 0x%02x, "
			       "ID 0x%08lx\n",
			       i,
			       rec->offset,
			       rec->attributes,
			       rec->id);

		/* Append the new record to the database */
		pdb_AppendRecord(db, rec); 
		db->numrecs = totalrecs;	/* Kludge */
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
			next_off = db->rec_index.rsrc->offset;
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
			"Expected 0x%lx, but we're at 0x%lx\n",
			db->appinfo_offset, (long) offset);

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
	PDB_TRACE(6)
		debug_dump(stdout, "<APP", db->appinfo, db->appinfo_len);

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
			next_off = db->rec_index.rsrc->offset;
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
			"Expected 0x%lx, but we're at 0x%lx\n",
			db->sortinfo_offset, (long) offset);

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
	PDB_TRACE(6)
		debug_dump(stdout, "<SORT", db->sortinfo, db->sortinfo_len); 

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
	for (i = 0, rsrc = db->rec_index.rsrc;
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

		PDB_TRACE(5)
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
				"Expected 0x%lx, but we're at 0x%lx\n",
				i,
				rsrc->offset, (long) offset);
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
		PDB_TRACE(6)
		{
			printf("Contents of resource %d:\n", i);
			debug_dump(stdout, "<RSRC", rsrc->data,
				   rsrc->data_len);
		}
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

		PDB_TRACE(5)
			printf("Reading record %d (id 0x%08lx)\n", i, rec->id);

		/* Out of paranoia, make sure we're in the right place */
		offset = lseek(fd, 0, SEEK_CUR);
					/* Find out where we are now */
		if (offset != rec->offset)
		{
			fprintf(stderr, "Warning: record %d isn't where I thought it would be.\n"
				"Expected 0x%lx, but we're at 0x%lx\n",
				i,
				rec->offset, (long) offset);
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
		 * that's more likely to be immune from fencepost errors.
		 * The two should, however, be equivalent. In fact, it
		 * might be a Good Thing to add a check to make sure.
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

		PDB_TRACE(6)
		{
			printf("Contents of record %d:\n", i);
			debug_dump(stdout, "<REC", rec->data, rec->data_len);
		}
	}

	return 0;		/* Success */
}

/* pdb_DownloadResources
 * Download a resource database's resources from the Palm, and put them in
 * 'db'.
 * Occasionally, this will produce a file different from the one that
 * 'pilot-xfer -b' does. With a blank xcopilot (i.e., delete the RAM and
 * scratch files), do a backup with 'pilot-xfer -b'. Then delete the RAM
 * and scratch files and do a backup with 'coldsync -b'. The file "Unsaved
 * Preferences.prc" produced by ColdSync will have an additional resource,
 * of type "psys" and ID 1; the Palm headers seem to indicate that this is
 * a password. The file produced by 'pilot-xfer' doesn't have this
 * resource.
 * I'd like to think that this means that ColdSync is better than
 * pilot-xfer, but it could just as easily be an off-by-one error or a
 * different set of flags.
 */
static int
pdb_DownloadResources(struct PConnection *pconn,
		      ubyte dbh,
		      struct pdb *db)
{
	int i;
	int err;
	uword totalrsrcs;	/* The real number of resources in the
				 * database.
				 */

	totalrsrcs = db->numrecs;	/* Get the number of resources now.
					 * It is necessary to remember this
					 * now because dlp_AppendResource()
					 * increments db->numrecs in the
					 * name of convenience.
					 */

	/* Read each resource in turn */
	for (i = 0; i < totalrsrcs; i++)
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

		PDB_TRACE(5)
		{
			fprintf(stderr, "DLP resource data %d:\n", i);
			fprintf(stderr, "\ttype: '%c%c%c%c'\n",
				(char) ((resinfo.type >> 24) & 0xff),
				(char) ((resinfo.type >> 16) & 0xff),
				(char) ((resinfo.type >> 8) & 0xff),
				(char) (resinfo.type & 0xff));
			fprintf(stderr, "\tid: %d\n", resinfo.id);
			fprintf(stderr, "\tindex: %d\n", resinfo.index);
			fprintf(stderr, "\tsize: %d\n", resinfo.size);
		}

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
		PDB_TRACE(6)
			debug_dump(stderr, "RSRC", rsrc->data, rsrc->data_len);

		/* Append the resource to the database */
		pdb_AppendResource(db, rsrc);
		db->numrecs = totalrsrcs;	/* Kludge */
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
	uword totalrecs;	/* The real number of records in the
				 * database.
				 */

	totalrecs = db->numrecs;	/* Get the number of records in the
					 * database. It is necessary to
					 * remember this here because
					 * dlp_AppendResource() increments
					 * db->numrecs in the name of
					 * convenience.
					 */

	/* Handle the easy case first: if there aren't any records, don't
	 * bother asking for their IDs.
	 */
	if (totalrecs == 0)
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
	if ((recids = (udword *) calloc(totalrecs, sizeof(udword)))
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
	if (numrecs != totalrecs)
	{
		fprintf(stderr, "### Whoa! numrecs is %d, but ReadRecordIDList says %d!\n",
			totalrecs, numrecs);
		/* XXX - What to do in this case? For now, just punt. */
		free(recids);
		return -1;
	}

	/* Read each record in turn */
	for (i = 0; i < totalrecs; i++)
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

		PDB_TRACE(6)
		{
			fprintf(stderr, "DLP record data %d:\n", i);
			fprintf(stderr, "\tid: 0x%08lx\n", recinfo.id);
			fprintf(stderr, "\tindex: %d\n", recinfo.index);
			fprintf(stderr, "\tsize: %d\n", recinfo.size);
			fprintf(stderr, "\tattributes: 0x%02x\n",
				recinfo.attributes);
			fprintf(stderr, "\tcategory: %d\n", recinfo.category); 
		}

		/* Fill in the record index data */
		rec->offset = 0L;	/* For now */
					/* XXX - Should this be filled in? */
		rec->attributes = recinfo.attributes;
		rec->id = recinfo.id;

		/* Fill in the data size entry */
		rec->data_len = recinfo.size;

		if (rec->data_len == 0)
		{
			rec->data = NULL;
			PDB_TRACE(6)
				fprintf(stderr, "REC: No record data\n");
		} else {
			/* Allocate space in 'rec' for the record data
			 * itself
			 */
			if ((rec->data = (ubyte *) malloc(rec->data_len))
			    == NULL)
			{
				fprintf(stderr, "pdb_DownloadRecords: out of memory.\n");
				free(recids);
				return -1;
			}

			/* Copy the record data to 'rec' */
			memcpy(rec->data, rptr, rec->data_len);
			PDB_TRACE(6)
				debug_dump(stderr, "REC", rec->data,
					   rec->data_len);
		}

		/* Append the record to the database */
		pdb_AppendRecord(db, rec);
		db->numrecs = totalrecs;	/* Kludge */
	}

	free(recids);

	return 0;	/* Success */
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
