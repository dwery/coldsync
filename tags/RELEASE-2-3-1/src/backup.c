/* backup.c
 *
 * Functions for backing up Palm databases (both .pdb and .prc) from
 * the Palm to the desktop.
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: backup.c,v 2.36 2001-10-06 21:58:00 arensb Exp $
 */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>		/* For malloc() */
#include <fcntl.h>		/* For open() */
#include <string.h>		/* For strncpy(), strncat() */
#include <ctype.h>		/* For isprint() */

#if HAVE_LIBINTL_H
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL_H */

#include "pconn/pconn.h"
#include "pdb.h"
#include "coldsync.h"
#include "cs_error.h"

static int download_resources(PConnection *pconn, ubyte dbh, struct pdb *db);
static int download_records(PConnection *pconn, ubyte dbh, struct pdb *db);

/* download_database
 * Download a database from the Palm. The returned 'struct pdb' is
 * allocated by download_database(), and the caller has to free it.
 */
struct pdb *
download_database(PConnection *pconn,
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
		fprintf(stderr, _("%s: can't allocate pdb.\n"),
			"download_database");
		/* XXX - Set cs_errno? */
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
	SYNC_TRACE(4)
	{
		fprintf(stderr, "download_database:\n");
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
	/* XXX - Check err more thoroughly */
	if (err != (int) DLPSTAT_NOERR)
	{
		fprintf(stderr, _("%s: Can't read database info: %d.\n"),
			"download_database",
			err);
		DlpCloseDB(pconn, dbh);	/* Don't really care if this fails */
		free_pdb(retval);
		/* XXX - Set cs_errno? */
		return NULL;
	}
	retval->next_reclistID = 0L;
	retval->numrecs = opendbinfo.numrecs;
	SYNC_TRACE(4)
	{
		fprintf(stderr, "\n\tnextID: %ld\n", retval->next_reclistID);
		fprintf(stderr, "\tlen: %d\n", retval->numrecs);
	}

	/* Try to get the AppInfo block */
	err = DlpReadAppBlock(pconn, dbh, 0, DLPC_APPBLOCK_TOEND,
			      &appinfo_len, &rptr);
	switch ((dlp_stat_t) err)
	{
	    case DLPSTAT_NOERR:
		/** Make a copy of the AppInfo block **/
		/* Allocate space for the AppInfo block */
		if ((retval->appinfo = (ubyte *) malloc(appinfo_len))
		    == NULL)
		{
			fprintf(stderr, _("%s: Out of memory.\n"),
				"download_database");
			DlpCloseDB(pconn, dbh);	/* Don't really care if
						 * this fails */
			free_pdb(retval);
			/* XXX - Set cs_errno? */
			return NULL;
		}
		memcpy(retval->appinfo, rptr, appinfo_len);
					/* Copy the AppInfo block */
		retval->appinfo_len = appinfo_len;
		SYNC_TRACE(4)
			fprintf(stderr,
				"download_database: got an AppInfo block\n");
		SYNC_TRACE(6)
			debug_dump(stderr, "APP", retval->appinfo,
				   retval->appinfo_len);
		break;
	    case DLPSTAT_NOTFOUND:
		/* This database doesn't have an AppInfo block */
		retval->appinfo_len = 0;
		retval->appinfo = NULL;
		SYNC_TRACE(5)
			fprintf(stderr, "download_databas: this db doesn't "
				"have an AppInfo block\n");
		/* XXX - Set cs_errno? */
		break;
	    default:
		fprintf(stderr, _("%s: Can't read AppInfo block for %s: "
				  "%d.\n"),
			"download_database",
			dbinfo->name, err);
		DlpCloseDB(pconn, dbh);	/* Don't really care if this fails */
		free_pdb(retval);
		/* XXX - Set cs_errno? */
		return NULL;
	}

	/* Try to get the sort block */
	err = DlpReadSortBlock(pconn, dbh, 0, DLPC_SORTBLOCK_TOEND,
			       &sortinfo_len, &rptr);
	switch ((dlp_stat_t) err)
	{
	    case DLPSTAT_NOERR:
		/** Make a copy of the sort block **/
		/* Allocate space for the sort block */
		if ((retval->sortinfo = (ubyte *) malloc(sortinfo_len))
		    == NULL)
		{
			fprintf(stderr, _("%s: Out of memory.\n"),
				"download_database");
			DlpCloseDB(pconn, dbh);	/* Don't really care if
						 * this fails */
			free_pdb(retval);
			return NULL;
		}
		memcpy(retval->sortinfo, rptr, retval->sortinfo_len);
					/* Copy the sort block */
		retval->sortinfo_len = sortinfo_len;
		/* XXX - Set cs_errno? */
		break;
	    case DLPSTAT_NOTFOUND:
		/* This database doesn't have a sort block */
		retval->sortinfo_len = 0;
		retval->sortinfo = NULL;
		/* XXX - Set cs_errno? */
		break;
	    default:
		fprintf(stderr, _("%s: Can't read sort block for %s: %d.\n"),
			"download_database",
			dbinfo->name, err);
		DlpCloseDB(pconn, dbh);	/* Don't really care if this fails */
		free_pdb(retval);
		/* XXX - Set cs_errno? */
		return NULL;
	}

	/* Download the records/resources */
	if (DBINFO_ISRSRC(dbinfo))
		err = download_resources(pconn, dbh, retval);
	else
		err = download_records(pconn, dbh, retval);
	SYNC_TRACE(7)
		fprintf(stderr,
			"After download_{resources,records}; err == %d\n",
			err);
	if (err < 0)
	{
		fprintf(stderr, _("Can't download record or resource "
				  "index.\n"));
		DlpCloseDB(pconn, dbh);	/* Don't really care if this fails */
		free_pdb(retval);
		/* XXX - Set cs_errno? */
		return NULL;
	}

	return retval;			/* Success */
}

/* download_resources
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
download_resources(PConnection *pconn,
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
					 * now because pdb_AppendResource()
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
			fprintf(stderr, _("%s: Out of memory.\n"),
				"download_resources");
			/* XXX - Set cs_errno? */
			return -1;
		}

		/* Download the 'i'th resource from the Palm */
		err = DlpReadResourceByIndex(pconn, dbh, i, 0,
					     DLPC_RESOURCE_TOEND,
					     &resinfo,
					     &rptr);

		/* XXX - Check err more thoroughly */
		if (err != (int) DLPSTAT_NOERR)
		{
			fprintf(stderr, _("Can't read resource %d: %d.\n"),
				i, err);
			free(rsrc);
			return -1;
		}

		SYNC_TRACE(5)
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
		/* XXX - Probably ought to use new_Resource() */
		rsrc->type = resinfo.type;
		rsrc->id = resinfo.id;
		rsrc->offset = 0L;	/* For now */

		/* Fill in the data size entry */
		rsrc->data_len = resinfo.size;

		/* Allocate space in 'rsrc' for the resource data itself */
		if ((rsrc->data = (ubyte *) malloc(rsrc->data_len)) == NULL)
		{
			fprintf(stderr, _("%s: Out of memory.\n"),
				"download_resources");
			free(rsrc);
			/* XXX - Set cs_errno */
			return -1;
		}

		/* Copy the resource data to 'rsrc' */
		memcpy(rsrc->data, rptr, rsrc->data_len);
		SYNC_TRACE(6)
			debug_dump(stderr, "RSRC", rsrc->data, rsrc->data_len);

		/* Append the resource to the database */
		pdb_AppendResource(db, rsrc);	/* XXX - Error-checking */
		db->numrecs = totalrsrcs;	/* Kludge */
	}

	return 0;	/* Success */
}

static int
download_records(PConnection *pconn,
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
					 * pdb_AppendResource() increments
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
		fprintf(stderr, _("Can't allocate list of record IDs.\n"));
		return -1;
	}

	/* Read the list of record IDs. DlpReadRecordIDList() might not be
	 * able to read all of them at once (it seems to have a limit of
	 * 500 or so), so we might need to read them a chunk at a time.
	 */
	numrecs = 0;
	while (numrecs < totalrecs)
	{
		uword num_read;		/* # of record IDs read this time */

		SYNC_TRACE(4)
			fprintf(stderr, "download_records: Reading a chunk "
				"of record IDs starting at %d\n",
				numrecs);

		/* Get the list of record IDs, as described above */
		if ((err = DlpReadRecordIDList(pconn, dbh, 0,
					       numrecs, totalrecs-numrecs,
					       &num_read, recids+numrecs))
		    != (int) DLPSTAT_NOERR)
		{
			/* XXX - Check err more thoroughly */
			fprintf(stderr, _("Can't read record ID list.\n"));
			free(recids);
			/* XXX - Set cs_errno */
			return -1;
		}

		/* Sanity check */
		if (num_read <= 0)
		{
			fprintf(stderr, _("DlpReadRecordIDList() read 0 "
					  "records. What happened?\n"));
			free(recids);
			return -1;
		}

		numrecs += num_read;
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
			fprintf(stderr, _("%s: Out of memory.\n"),
				"download_records");
			free(recids);
			return -1;
		}

		/* Download the 'i'th record from the Palm */
		err = DlpReadRecordByID(pconn, dbh,
					recids[i],
					0, DLPC_RECORD_TOEND,
					&recinfo,
					&rptr);
		/* XXX - Check err more thoroughly */
		if (err != (int) DLPSTAT_NOERR)
		{
			fprintf(stderr, _("Can't read record %d: %d.\n"),
				i, err);
			free(recids);
			/* XXX - Set cs_errno */
			return -1;
		}

		SYNC_TRACE(6)
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
		rec->flags = recinfo.attributes;
		rec->category = recinfo.category;
		rec->id = recinfo.id;

		/* Fill in the data size entry */
		rec->data_len = recinfo.size;

		if (rec->data_len == 0)
		{
			rec->data = NULL;
			SYNC_TRACE(6)
				fprintf(stderr, "REC: No record data\n");
		} else {
			/* Allocate space in 'rec' for the record data
			 * itself
			 */
			if ((rec->data = (ubyte *) malloc(rec->data_len))
			    == NULL)
			{
				fprintf(stderr, _("%s: Out of memory.\n"),
					"download_records");
				free(recids);
				/* XXX - Set cs_errno? */
				return -1;
			}

			/* Copy the record data to 'rec' */
			memcpy(rec->data, rptr, rec->data_len);
			SYNC_TRACE(6)
				debug_dump(stderr, "REC", rec->data,
					   rec->data_len);
		}

		/* Append the record to the database */
		pdb_AppendRecord(db, rec);	/* XXX - Error-checking */
		db->numrecs = totalrecs;	/* Kludge */
	}

	free(recids);

	return 0;	/* Success */
}

/* XXX - Temporary name */
/* Back up a single file */
int
backup(PConnection *pconn,
       const struct dlp_dbinfo *dbinfo,
       const char *dirname)		/* Directory to back up to */
{
	int err;
	const char *bakfname;		/* Backup pathname */
	int bakfd;			/* Backup file descriptor */
	struct pdb *pdb;		/* Database downloaded from Palm */
	ubyte dbh;			/* Database handle (on Palm) */

	bakfname = mkpdbname(dirname, dbinfo, True);
				/* Construct the backup file name */

	Verbose(1, _("Backing up \"%s\""), dbinfo->name);

	/* Create and open the backup file */
	/* XXX - Is the O_EXCL flag desirable? */
	bakfd = open((const char *) bakfname,
		     O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0600);
	if (bakfd < 0)
	{
		Error(_("%s: can't create new backup file %s.\n"
			"It may already exist."),
		      "backup",
		      bakfname);
		Perror("open");
		va_add_to_log(pconn, "%s %s - %s\n",
			      _("Backup"), dbinfo->name, _("Error"));
		return -1;
	}
	/* XXX - Lock the file */

	/* Open the database on the Palm */
	err = DlpOpenConduit(pconn);
	switch ((dlp_stat_t) err)
	{
	    case DLPSTAT_NOERR:		/* No error */
		break;
	    case DLPSTAT_CANCEL:	/* There was a pending cancellation
					 * by user, on the Palm */
		Error(_("Cancelled by Palm."));
		cs_errno = CSE_CANCEL;
		close(bakfd);
		unlink(bakfname);
		va_add_to_log(pconn, "%s %s - %s\n",
			      _("Backup"), dbinfo->name, _("Cancelled"));
		return -1;
	    default:			/* All other errors */
		switch (palm_errno)
		{
		    case PALMERR_TIMEOUT:
			cs_errno = CSE_NOCONN;
			break;
		    default:
			break;
		}

		Error(_("Can't open backup conduit."));
		close(bakfd);
		va_add_to_log(pconn, "%s %s - %s\n",
			      _("Backup"), dbinfo->name, _("Error"));
		return -1;
	}

	err = DlpOpenDB(pconn,
			CARD0,
			dbinfo->name,
			DLPCMD_MODE_READ | DLPCMD_MODE_SECRET,
			/* This is just a backup; we're not going to be
			 * modifying anything, so there's no reason to open
			 * the database read-write.
			 */
			/* "Secret" records aren't actually secret. They're
			 * actually just the ones marked private, and
			 * they're not at all secret.
			 */
			&dbh);
	if (err != (int) DLPSTAT_NOERR)
	{
		switch (palm_errno)
		{
		    case PALMERR_TIMEOUT:
			cs_errno = CSE_NOCONN;
			break;
		    default:
			break;
		}

		Error(_("Can't open database \"%s\"."),
		      dbinfo->name);
		close(bakfd);
		va_add_to_log(pconn, "%s %s - %s\n",
			      _("Backup"), dbinfo->name, _("Error"));
		return -1;
	}

	/* Download the database from the Palm */
	pdb = download_database(pconn, dbinfo, dbh);
	if (pdb == NULL)
	{
		/* Error downloading the file.
		 * We don't send an error message to the Palm because
		 * typically the problem is that the connection to the Palm
		 * was lost.
		 */
		switch (palm_errno)
		{
		    case PALMERR_TIMEOUT:
			cs_errno = CSE_NOCONN;
			break;
		    default:
			break;
		}

		err = DlpCloseDB(pconn, dbh);
		unlink(bakfname);	/* Delete the zero-length backup
					 * file */
		close(bakfd);
		va_add_to_log(pconn, "%s %s - %s\n",
			      _("Backup"), dbinfo->name, _("Error"));
		return -1;
	}
	SYNC_TRACE(7)
		fprintf(stderr, "After download_database\n");
	DlpCloseDB(pconn, dbh);
	SYNC_TRACE(7)
		fprintf(stderr, "After DlpCloseDB\n");

	/* Write the database to the backup file */
	err = pdb_Write(pdb, bakfd);
	if (err < 0)
	{
		Error(_("%s: can't write database \"%s\" to \"%s\"."),
		      "backup",
		      dbinfo->name, bakfname);
		err = DlpCloseDB(pconn, dbh);
		free_pdb(pdb);
		close(bakfd);
		va_add_to_log(pconn, "%s %s - %s\n",
			      _("Backup"), dbinfo->name, _("Error"));
		return -1;
	}
	SYNC_TRACE(3)
		fprintf(stderr, "Wrote \"%s\" to \"%s\"\n",
			dbinfo->name, bakfname);

	err = DlpCloseDB(pconn, dbh);
	free_pdb(pdb);
	close(bakfd);
	va_add_to_log(pconn, "%s %s - %s\n",
		      _("Backup"), dbinfo->name, _("OK"));
	return 0;
}

/* full_backup
 * Do a full backup of every database on the Palm to
 * 'global_opts.backupdir'.
 */
int
full_backup(PConnection *pconn,
	    struct Palm *palm,
	    const char *backupdir)
{
	int err;
	const struct dlp_dbinfo *cur_db;

	SYNC_TRACE(1)
		fprintf(stderr, "Inside full_backup() -> \"%s\"\n",
			backupdir);

	err = palm_fetch_all_DBs(palm);
	if (err < 0)
		return -1;

	palm_resetdb(palm);
	while ((cur_db = palm_nextdb(palm)) != NULL)
	{
		err = backup(pconn, cur_db, backupdir);
		if (err < 0)
		{
			Error(_("%s: Can't back up \"%s\"."),
			      "full_backup",
			      cur_db->name);

			/* If the problem is a known fatal error, abort.
			 * Otherwise, hope that the problem was transient,
			 * and continue.
			 */
			switch (cs_errno)
			{
			    case CSE_NOCONN:
			    case CSE_CANCEL:
				return -1;
			    default:
				break;
			}
		}
	}
	return 0;
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
