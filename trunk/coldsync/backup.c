/* backup.c
 *
 * Functions for backing up Palm databases (both .pdb and .prc) from
 * the Palm to the desktop.
 *
 * $Id: backup.c,v 1.1 1999-02-22 10:27:13 arensb Exp $
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include "config.h"
#include "palm/palm_types.h"
#include "pconn/palm_errno.h"
#include "pconn/dlp_cmd.h"
#include "pconn/util.h"
#include "palm/pdb.h"
#include "coldsync.h"

#if 0
int
Cold_BackupDB(struct PConnection *pconn,
	      struct ColdPalm *palm,
	      struct dlp_dbinfo *dbinfo,
	      const char *fname)
{
extern int padp_debug;
extern int dlp_debug;
extern int dlpc_debug;
	int i;
	int err;
	int outfd;		/* Backup file descriptor */
	static ubyte outbuf[2048];	/* XXX - Figure out how big this ought to be */
	const ubyte *rptr;	/* Pointer into buffers, for reading */
	ubyte *wptr;		/* Pointer into buffers, for writing */
	ubyte dbh;		/* Database handle */
	struct dlp_opendbinfo opendbinfo;
				/* Info about the currently-open database */
	uword num;
	struct dlp_appblock appblock;	/* AppInfo block */
	uword appblock_len;
	ubyte *appblock_data;
	struct dlp_sortblock sortblock;	/* Sort block */
	uword sortblock_len;
	ubyte *sortblock_data;
	udword *idlist;		/* Array of record IDs */

/* XXX - Should abort more cleanly on error: let the Palm know and stuff */

	printf("** Backing up \"%s\" to \"%s\"\n",
	       dbinfo->name, fname);
padp_debug = 0;
dlp_debug = 10;
dlpc_debug = 10;

	/* XXX - Would it be easier to read the entire database into memory
	 * first, and then write it to the file? Probably.
	 */

	/* Tell the Palm we're running a conduit. Yes, HotSync does this
	 * before opening the database.
	 */
	if ((err = DlpOpenConduit(pconn)) < 0)
	{
		fprintf(stderr, "Can't OpenConduit(\"%s\"): err %d, palm_errno %d\n",
			dbinfo->name, err, palm_errno);
		return -1;
	}

	/* Open the database */
	/* XXX - Memory card 0 is hard-wired. Bad! */
	err = DlpOpenDB(pconn, 0, dbinfo->name, DLPCMD_OPENDBFLAG_READ, &dbh);
	if (err < 0)
	{
		fprintf(stderr, "Can't open Palm database \"%s\": err %d, palm_errno %d\n",
			dbinfo->name, err, palm_errno);
		return -1;
	}

	/* XXX - Ought to be more paranoid: back the database up to a
	 * temporary file, then move it into place once the backup has
	 * completed successfully. That way, if this fails during a full
	 * backup when there are already databases in place, the
	 * information that's already there won't get clobbered.
	 */
	/* Create the file */
	outfd = open(fname, O_RDWR | O_CREAT /*| O_EXLOCK*/, 0666);
	if (outfd < 0)
	{
		perror("Cold_BackupDB: open");
		return -1;
	}

	/* Find out how many records/resources there are in the database */
	err = DlpReadOpenDBInfo(pconn, dbh, &opendbinfo);
	if (err < 0)
	{
		fprintf(stderr, "Error getting open DB info for \"%s\": %d\n",
			dbinfo->name, err);
		/* XXX - Delete the file */
		return -1;
	}
printf("There are %d records\n", opendbinfo.numrecs);

	/* Get the AppInfo block, if it exists */
	appblock.dbid = dbh;
	appblock.offset = 0;
	appblock.len = ~0;	/* Read the whole thing */
	appblock_len = 0;
	if ((err = DlpReadAppBlock(pconn, &appblock, &appblock_len, &rptr)) < 0)
	{
		fprintf(stderr, "Error reading AppBlock \"%s\": %d\n",
			dbinfo->name, err);
/*  		return -1; */
	}

	/* Save a copy of the app block */
	appblock_data = NULL;
	if (appblock_len > 0)
	{
		if ((appblock_data = (ubyte *) malloc(appblock_len)) == NULL)
		{
			perror("appblock malloc");
			return -1;
		}
		memcpy(appblock_data, rptr, appblock_len);
	}

	/* Get the sort block, if it exists */
	sortblock.dbid = dbh;
	sortblock.offset = 0;
	sortblock.len = ~0;	/* Read the whole thing */
	sortblock_len = 0;
	if ((err = DlpReadSortBlock(pconn, &sortblock, &sortblock_len, &rptr)) < 0)
	{
		fprintf(stderr, "Error reading SortBlock \"%s\": %d\n",
			dbinfo->name, err);
/*  		return -1; */
	}
	/* Save a copy of the sort block */
	sortblock_data = NULL;
	if (sortblock_len > 0)
	{
		if ((sortblock_data = (ubyte *) malloc(sortblock_len)) == NULL)
		{
			perror("sortblock malloc");
			return -1;
		}
		memcpy(sortblock_data, rptr, sortblock_len);
	}

	/* XXX - Get list of record IDs */
	if (((dbinfo->db_flags & DLPCMD_DBFLAG_RESDB) == 0) &&
	    opendbinfo.numrecs > 0)
	{
		/* Allocate space for the list of record IDs */
		if ((idlist = (udword *) malloc(opendbinfo.numrecs * sizeof(udword)))
		    == NULL)
		{
			perror("Can't allocate record ID list");
			return -1;
		}
		err = DlpReadRecordIDList(pconn, dbh, 0x00, 0, ~0, &num, idlist);
		if (err < 0)
		{
			fprintf(stderr, "Error in ReadRecordIDList \"%s\": %d\n",
				dbinfo->name, err);
		}
printf("# Record list:\n");
for (i = 0; i < opendbinfo.numrecs; i++)
printf("\t%d: 0x%08lx\n", i, idlist[i]);
	}

	/* XXX - Write what we've got so far */
	wptr = outbuf;
	memcpy(wptr, dbinfo->name, PDB_DBNAMELEN);
	wptr += PDB_DBNAMELEN;
	put_uword(&wptr, dbinfo->db_flags);
	put_uword(&wptr, dbinfo->version);
	put_udword(&wptr, time_dlp2palmtime(&dbinfo->ctime));
	put_udword(&wptr, time_dlp2palmtime(&dbinfo->mtime));
	put_udword(&wptr, time_dlp2palmtime(&dbinfo->baktime));
	put_udword(&wptr, dbinfo->modnum);
	put_udword(&wptr, 0L);	/* appinfoID: this will be filled in later */
	put_udword(&wptr, 0L);	/* sortinfoID: this will be filled in later */
	put_udword(&wptr, dbinfo->type);
	put_udword(&wptr, dbinfo->creator);
	put_udword(&wptr, 0L);	/* uniqueIDseed */
				/* XXX - Is there a good way to get this?
				 * Should this just be a random number?
				 */
	/* Write the database header */
	/* XXX - What should write()'s size argument be? */
	if ((err = write(outfd, outbuf, wptr - outbuf)) < 0)
	{
		perror("Cold_BackupDB: write");
		close(outfd);
		/* XXX - Delete the file */
		return -1;
	}

	/* Construct the record/resource index header */
	wptr = outbuf;
	put_udword(&wptr, 0L);		/* nextID: when is it not 0? */
	put_uword(&wptr, opendbinfo.numrecs);

	/* Write the record/resource index header */
	/* XXX - Fixed size for write()'s size argument */
	if ((err = write(outfd, outbuf, wptr - outbuf)) < 0)
	{
		perror("Cold_BackupDB: write");
		close(outfd);
		/* XXX - Delete the file */
		return -1;
	}

	/* Leave space for record/resource index; we'll come back and fill
	 * it in later.
	 */
	if (dbinfo->db_flags & DLPCMD_DBFLAG_RESDB)
	{
		/* This is a resource database */
		/* XXX */
/*  		lseek(fd, , SEEK_CUR); */
	} else {
		/* This is a record database */
		/* XXX */
	}

	/* Write the AppInfo block, if any */
	if (appblock_len > 0)
	{
		write(outfd, appblock_data, appblock_len);
		/* XXX - Error-checking */
	}

	/* Write the sort block, if any */
	if (sortblock_len > 0)
	{
		write(outfd, sortblock_data, sortblock_len);
		/* XXX - Error-checking */
	}

	/* XXX - Read each record in turn */
	if (((dbinfo->db_flags & DLPCMD_DBFLAG_RESDB) == 0) &&
	    opendbinfo.numrecs > 0)
	{
		for (i = 0; i < opendbinfo.numrecs; i++)
		{
			struct dlp_readrecreq_byid byid;	/* XXX - Bogus API */
			struct dlp_readrecret record;

			/* Read a record */
			byid.dbid = dbh;
			byid.recid = idlist[i];
			byid.offset = 0;
			/* XXX - This length is bogus, and is just here to
			 * work around the fact that the PADP layer doesn't
			 * handle multi-fragment packets yet.
			 * XXX - This should really loop and read each bit
			 * of the record in turn (for now).
			 */
			byid.len = 512;		/* XXX - Need const for "everything"*/
			fprintf(stderr, "Reading record ID 0x%08lx\n", byid.recid);
			if ((err = DlpReadRecordByID(pconn, &byid, &record)) < 0)
			{
				fprintf(stderr, "Error in ReadRecordByID \"%s\": %d\n",
					dbinfo->name, err);
				/* XXX - Better error handling */
				return -1;
			}

			/* Write the record */
			/* XXX - Error-checking */
			write(outfd, record.data, record.size);
		}
	}

	/* Close the file */
	close(outfd);

	/* XXX - Reset the sync flags */

	if ((err = DlpCloseDB(pconn, dbh)) < 0)
	{
		fprintf(stderr, "Error closing Palm database \"%s\": %d\n",
			dbinfo->name, err);
		return -1;
	}

	return 0;
}
#endif	/* 0 */

int
Cold_RecordBackup(struct PConnection *pconn,
		  struct ColdPalm *palm,
		  struct dlp_dbinfo *dbinfo,
		  char *bakfname)
{
	int i;
	int err;
	ubyte dbh = ~0;		/* Database handle */
	const ubyte *rptr;	/* Pointer into buffers, for reading */
	uword num;		/* Generic: # things read */
	udword ctime;		/* Creation time, in seconds */
	udword mtime;		/* Modification time, in seconds */
	udword baktime;		/* Last backup time, in seconds */
	uword appblock_len = 0;	/* Length of AppInfo block */
	ubyte *appblock = NULL;	/* AppInfo block */
	uword sortblock_len = 0;
				/* Length of sort block */
	ubyte *sortblock = NULL;
				/* Sort block */
	struct dlp_opendbinfo opendbinfo;
				/* Info about open database (well, the # of
				 * records in it). */
	uword num_recs = 0;	/* # records in the database */
	udword *recids = NULL;	/* Array of record IDs */
	struct dlp_recinfo *recinfo = NULL;
				/* Info struct for each record */
	ubyte **recdata = NULL;	/* Data for each record */

printf("--> Backing up record database %s to %s\n",
       dbinfo->name, bakfname);

	/* Tell the Palm we're synchronizing a database */
	err = DlpOpenConduit(pconn);
	if (err != DLPSTAT_NOERR)
	{
		err = -1;
		goto abort;
	}

	/* Open the database */
	/* XXX - card #0 is hardwired */
	err = DlpOpenDB(pconn, 0, dbinfo->name,
			DLPCMD_MODE_READ /*| DLPCMD_MODE_SECRET*/,
			&dbh);
	switch (err)
	{
	    case DLPSTAT_NOERR:
		break;
	    default:
		fprintf(stderr, "*** Can't open %s: %d\n", dbinfo->name, err);
		return -1;
	}

	/* XXX - Header info:
	   name: dbinfo->name
	   attributes: dbinfo->db_flags
	   version: dbinfo->version
	   ctime (as time_t): need to convert dbinfo->ctime
	   mtime (as time_t): need to convert dbinfo->mtime
	   baktime (as time_t): need to convert dbinfo->baktime
	   modnum: dbinfo->modnum
	   appinfo offset: try to get AppInfo block
	   sortinfo offset: try to get sort block
	   type (4 chars): dbinfo->type
	   creator (4 chars): dbinfo->creator
	   unique ID seed (?): set to something arbitrary (deadf00d?)	XXX
	 */
	/* Convert the times from DLP time structures to Palm-style
	 * time_ts.
	 */
	ctime = time_dlp2palmtime(&dbinfo->ctime);
	mtime = time_dlp2palmtime(&dbinfo->mtime);
	baktime = time_dlp2palmtime(&dbinfo->baktime);

	/* Try to get the AppInfo block */
	/* XXX - Need a const for "everything" */
	err = DlpReadAppBlock(pconn, dbh, 0, ~0, &appblock_len, &rptr);
	switch (err)
	{
	    case DLPSTAT_NOERR:
		break;
	    case DLPSTAT_NOTFOUND:
		/* This database doesn't have an AppInfo block */
printf("### %s doesn't have an AppInfo block\n", dbinfo->name);
		appblock_len = 0;
		appblock = NULL;
		break;
	    default:
		fprintf(stderr, "*** Can't read AppInfo block for %s: %d\n",
			dbinfo->name, err);
		err = -1;
		goto abort;
	}
	/* Make a copy of the AppInfo block */
	if ((appblock = (ubyte *) malloc(appblock_len)) == NULL)
	{
		fprintf(stderr, "** Out of memory\n");
		err = -1;
		goto abort;
	}
	memcpy(appblock, rptr, appblock_len);

	/* Try to get the sort block */
	/* XXX - Need a const for "everything" */
	err = DlpReadSortBlock(pconn, dbh, 0, ~0, &sortblock_len, &rptr);
	switch (err)
	{
	    case DLPSTAT_NOERR:
		break;
	    case DLPSTAT_NOTFOUND:
		/* This database doesn't have an SortInfo block */
printf("### %s doesn't have a sort block\n", dbinfo->name);
		sortblock_len = 0;
		sortblock = NULL;
		break;
	    default:
		fprintf(stderr, "*** Can't read sort block for %s: %d\n",
			dbinfo->name, err);
		err = -1;
		goto abort;
	}
	/* Make a copy of the sort block */
	if ((sortblock = (ubyte *) malloc(sortblock_len)) == NULL)
	{
		fprintf(stderr, "** Out of memory\n");
		err = -1;
		goto abort;
	}
	memcpy(sortblock, rptr, sortblock_len);

	/* XXX - Index header info:
	   nextID (probably safe to set to 0)
	   len - # of records: DlpOpenDBInfo
	 */

	/* Find out how many records there are in this database */
	err = DlpReadOpenDBInfo(pconn, dbh, &opendbinfo);
	if (err != DLPSTAT_NOERR)
	{
		err = -1;
		goto abort;
	}
	num_recs = opendbinfo.numrecs;	/* Just for convenience */
printf("  %s has %d records\n", dbinfo->name, num_recs);

	/** Get the record IDs **/
	/* Allocate space for the array of record IDs */
	if (num_recs > 0)
	{
		if ((recids = (udword *) calloc(num_recs, sizeof(udword)))
		    == NULL)
		{
			fprintf(stderr, "Out of memory\n");
			err = -1;
			goto abort;
		}
		/* Get the record IDs */
		/* XXX - Need flags */
		/* XXX - Need const for "everything" */
		err = DlpReadRecordIDList(pconn, dbh, 0x00, 0, ~0,
					  &num, recids);
		if (err != DLPSTAT_NOERR)
		{
			fprintf(stderr, "Can't get array of record IDs: %d\n",
				err);
			err = -1;
			goto abort;
		}

		/* Allocate space to store the record info structs */
		if ((recinfo = (struct dlp_recinfo *)
		     calloc(num_recs, sizeof(struct dlp_recinfo)))
		    == NULL)
		{
			fprintf(stderr, "Out of memory\n");
			err = -1;
			goto abort;
		}

		/* Allocate the array that'll hold pointers to the record
		 * data.
		 */
		if ((recdata = (ubyte **) calloc(num_recs,
						 sizeof(ubyte *)))
		    == NULL)
		{
			fprintf(stderr, "Out of memory\n");
			err = -1;
			goto abort;
		}

		/* Read the record info structs, and the record data */
		for (i = 0; i < num_recs; i++)
		{
extern int slp_debug;
extern int padp_debug;

slp_debug = 100;
padp_debug = 100;
			/* XXX - Need a const for "everything" */
			err = DlpReadRecordByID(pconn, dbh,
						recids[i],
						0, ~0,
						&(recinfo[i]),
						&rptr);
			if (err != DLPSTAT_NOERR)
			{
				fprintf(stderr, "Can't read record\n");
				err = -1;
				goto abort;
			}

			/* Get a copy of the record data */
			if ((recdata[i] = (ubyte *) malloc(recinfo[i].size))
			    == NULL)
			{
				fprintf(stderr, "Out of memory\n");
				err = -1;
				goto abort;
			}
			memcpy(recdata[i], rptr, recinfo[i].size);
		}
	}
	printf("Record IDs:\n");
	for (i = 0; i < num_recs; i++)
		printf("\t%d: 0x%08lx\n", i, recids[i]);

	/* XXX - Record index:
	   For each record:
	   - offset: determined by things' size
	   - attributes: DlpReadRecordByID
	   - unique ID: DlpReadRecordByID
	 */
	/* XXX - AppInfo block, if any */
	/* XXX - Sort block, if any */
	/* XXX - List of records */

  abort:	/* In case of error, the code above just jumps to here.
		 * Clean everything up in the reverse order of how it was
		 * initialized.
		 */
	if (recdata != NULL)		/* It's an array of pointers */
	{
		for (i = 0; i < num_recs; i++)
			if (recdata[i] != NULL)
				free(recdata[i]);
		free(recdata);
	}

	if (recinfo != NULL)
		free(recinfo);
	if (recids != NULL)
		free(recids);
	if (sortblock != NULL)
		free(sortblock);
	if (appblock != NULL)
		free(appblock);

	/* Close the database */
	DlpCloseDB(pconn, dbh);		/* Don't really care if this fails */

	return err;
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
