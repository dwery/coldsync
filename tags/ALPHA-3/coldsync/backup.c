/* backup.c
 *
 * Functions for backing up Palm databases (both .pdb and .prc) from
 * the Palm to the desktop.
 *
 * $Id: backup.c,v 1.4 1999-02-24 13:23:31 arensb Exp $
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>		/* For open() */
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <string.h>		/* For memcpy() et al. */
#include "config.h"
#include "palm/palm_types.h"
#include "pconn/palm_errno.h"
#include "pconn/dlp_cmd.h"
#include "pconn/util.h"
#include "palm/pdb.h"
#include "coldsync.h"

/* XXX - Can these two monster functions be broken down into smaller
 * pieces? */

/* XXX - This should build a struct pdb, then dump it to a file */
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
	ubyte *wptr;		/* Pointer into buffers, for writing */
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
	int outfd = -1;		/* Output file descriptor */
	struct dlp_opendbinfo opendbinfo;
				/* Info about open database (well, the # of
				 * records in it). */
	uword num_recs = 0;	/* # records in the database */
	udword *recids = NULL;	/* Array of record IDs */
	struct dlp_recinfo *recinfo = NULL;
				/* Info struct for each record */
	ubyte **recdata = NULL;	/* Data for each record */
	static ubyte header_buf[PDB_HEADER_LEN];
				/* Buffer for outgoing database header */
	static ubyte rl_header_buf[PDB_RECORDLIST_LEN];
				/* Buffer for outgoing record list header */
	static ubyte recix_buf[PDB_RECORDIX_LEN];
				/* Buffer for outgoing record info struct */
	udword next_offset;	/* Offset of the next thing that gets
				 * written */

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
		/* Make a copy of the AppInfo block */
		if ((appblock = (ubyte *) malloc(appblock_len)) == NULL)
		{
			fprintf(stderr, "** Out of memory\n");
			err = -1;
			goto abort;
		}
		memcpy(appblock, rptr, appblock_len);
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

	/* Try to get the sort block */
	/* XXX - Need a const for "everything" */
	err = DlpReadSortBlock(pconn, dbh, 0, ~0, &sortblock_len, &rptr);
	switch (err)
	{
	    case DLPSTAT_NOERR:
		/* Make a copy of the sort block */
		if ((sortblock = (ubyte *) malloc(sortblock_len)) == NULL)
		{
			fprintf(stderr, "** Out of memory\n");
			err = -1;
			goto abort;
		}
		memcpy(sortblock, rptr, sortblock_len);
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
/*  extern int slp_debug; */
/*  extern int padp_debug; */

/*  slp_debug = 100; */
/*  padp_debug = 100; */
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

	/* Open the backup file */
	if ((outfd = open(bakfname, O_WRONLY | O_CREAT, 0666)) < 0)
	{
		fprintf(stderr, "Can't open \"%s\" for writing\n",
			bakfname);
		perror("open");
		err = -1;
		goto abort;
	}

	/* Initialize next_offset: the first thing we'll need an offset for
	 * (probably the AppInfo block) will go after the two useless NULs.
	 */
	next_offset = PDB_HEADER_LEN + PDB_RECORDLIST_LEN +
		(num_recs * PDB_RECORDIX_LEN) + 2;

	/* Construct the database header */
	wptr = header_buf;
	memcpy(wptr, dbinfo->name, PDB_DBNAMELEN);	/* name */
	wptr += PDB_DBNAMELEN;
	/* Clear the open flag before writing */
	dbinfo->db_flags &= ~DLPCMD_DBFLAG_OPEN;
	put_uword(&wptr, dbinfo->db_flags);		/* attributes */
	put_uword(&wptr, dbinfo->version);		/* version */
	put_udword(&wptr, ctime);			/* ctime */
	put_udword(&wptr, mtime);			/* mtime */
	put_udword(&wptr, baktime);			/* baktime */
	put_udword(&wptr, dbinfo->modnum);		/* modnum */
	if (appblock == NULL)
		put_udword(&wptr, 0L);			/* appinfoID */
	else {
		put_udword(&wptr, next_offset);		/* appinfoID */
		next_offset += appblock_len;	/* Update next_offset */
	}
	if (sortblock == NULL)
{
fprintf(stderr, "no sortblock offset\n");
		put_udword(&wptr, 0L);			/* sortinfoID */
}
	else {
fprintf(stderr, "sortblock's offset: 0x%08lx\n", next_offset);
		put_udword(&wptr, next_offset);		/* sortinfoID */
		next_offset += sortblock_len;	/* Update next_offset */
	}
	put_udword(&wptr, dbinfo->type);		/* type */
	put_udword(&wptr, dbinfo->creator);		/* creator */
	put_udword(&wptr, 0L);				/* uniqueIDseed */

	/* Write the database header */
	if ((err = write(outfd, header_buf, PDB_HEADER_LEN)) != PDB_HEADER_LEN)
	{
		perror("write");
		goto abort;
	}

	/* Construct the record index header */
	wptr = rl_header_buf;
	put_udword(&wptr, 0L);		/* nextID */
					/* XXX - Should this be something else? */
	put_uword(&wptr, num_recs);	/* len */

	/* Write the record index header */
	if ((err = write(outfd, rl_header_buf, PDB_RECORDLIST_LEN)) !=
	    PDB_RECORDLIST_LEN)
	{
		perror("write");
		goto abort;
	}

	/* Write the record index, one record at a time */
	for (i = 0; i < num_recs; i++)
	{
		wptr = recix_buf;
		put_udword(&wptr, next_offset);	/* offset */
		next_offset += recinfo[i].size;	/* Update next_offset */
		put_ubyte(&wptr, recinfo[i].attributes);
						/* attributes */
		/* Lowest 3 bytes of uniqueID */
		put_ubyte(&wptr, (recinfo[i].id >> 16) & 0xff);
		put_ubyte(&wptr, (recinfo[i].id >> 8) & 0xff);
		put_ubyte(&wptr, recinfo[i].id & 0xff);

		/* Write the record index entry */
		if ((err = write(outfd, recix_buf, PDB_RECORDIX_LEN)) !=
		    PDB_RECORDIX_LEN)
		{
			perror("write");
			goto abort;
		}
	}

	/* Write those useless two NULs */
	wptr = header_buf;
	put_uword(&wptr, 0);
	if ((err = write(outfd, header_buf, 2)) < 0)
	{
		perror("write");
		goto abort;
	}

	/* Write the AppInfo block, if there is one */
	if (appblock != NULL)
		if ((err = write(outfd, appblock, appblock_len)) !=
		    appblock_len)
		{
			perror("write");
			goto abort;
		}

	/* Write the sort block, if there is one */
	if (sortblock != NULL)
		if ((err = write(outfd, sortblock, sortblock_len)) !=
		    sortblock_len)
		{
			perror("write");
			goto abort;
		}

	/* Write each record in turn */
	for (i = 0; i < num_recs; i++)
	{
		if ((err = write(outfd, recdata[i], recinfo[i].size)) !=
		    recinfo[i].size)
		{
			perror("write");
			goto abort;
		}
	}

	/* XXX - I'm not sure about these two, nor their relative order.
	 * HotSync appears to do both at different times.
	 */
/* XXX - It appears to work if you clean the database, only. Anything else
 * causes it to reset.
 */
#if 0
fprintf(stderr, "### Cleaning up database\n");
	err = DlpCleanUpDatabase(pconn, dbh);
	if (err != DLPSTAT_NOERR)
	{
		fprintf(stderr, "Can't clean up database.\n");
		err = -1;
		goto abort;
	}
#endif	/* 0 */

#if 0
fprintf(stderr, "### Resetting sync flags\n");
	err = DlpResetSyncFlags(pconn, dbh);
	if (err != DLPSTAT_NOERR)
	{
		fprintf(stderr, "Can't reset sync flags.\n");
		err = -1;
		goto abort;
	}
#endif	/* 0 */

  abort:	/* In case of error, the code above just jumps to here.
		 * Clean everything up in the reverse order of how it was
		 * initialized.
		 */
	if (outfd >= 0)			/* Close the file */
		close(outfd);

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
	if (dbh != 0xffff)
		DlpCloseDB(pconn, dbh);	/* Don't really care if this fails */

	return err;
}

/* XXX - This should build a struct pdb, then dump it to a file */
int
Cold_ResourceBackup(struct PConnection *pconn,
		    struct ColdPalm *palm,
		    struct dlp_dbinfo *dbinfo,
		    char *bakfname)
{
	int i;
	int err;
	ubyte dbh = ~0;		/* Database handle */
	const ubyte *rptr;	/* Pointer into buffers, for reading */
	ubyte *wptr;		/* Pointer into buffers, for writing */
/*  	uword num; */		/* Generic: # things read */
	udword ctime;		/* Creation time, in seconds */
	udword mtime;		/* Modification time, in seconds */
	udword baktime;		/* Last backup time, in seconds */
	uword appblock_len = 0;	/* Length of AppInfo block */
	ubyte *appblock = NULL;	/* AppInfo block */
	uword sortblock_len = 0;
				/* Length of sort block */
	ubyte *sortblock = NULL;
				/* Sort block */
	int outfd = -1;		/* Output file descriptor */
	struct dlp_opendbinfo opendbinfo;
				/* Info about open database (well, the # of
				 * resources in it). */
	uword num_rsrcs = 0;	/* # resources in the database */
/*  	udword *rsrcids = NULL; */	/* Array of resource IDs */
	struct dlp_resource *rsrcinfo = NULL;
				/* Info struct for each resource */
	ubyte **rsrcdata = NULL;	/* Data for each resource */
	static ubyte header_buf[PDB_HEADER_LEN];
				/* Buffer for outgoing database header */
	static ubyte rl_header_buf[PDB_RECORDLIST_LEN];
				/* Buffer for outgoing resource list header */
	static ubyte rsrcix_buf[PDB_RESOURCEIX_LEN];
				/* Buffer for outgoing resource info struct */
	udword next_offset;	/* Offset of the next thing that gets
				 * written */

printf("--> Backing up resource database %s to %s\n",
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
		/* Make a copy of the AppInfo block */
		if ((appblock = (ubyte *) malloc(appblock_len)) == NULL)
		{
			fprintf(stderr, "** Out of memory\n");
			err = -1;
			goto abort;
		}
		memcpy(appblock, rptr, appblock_len);
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

	/* Try to get the sort block */
	/* XXX - Need a const for "everything" */
	err = DlpReadSortBlock(pconn, dbh, 0, ~0, &sortblock_len, &rptr);
	switch (err)
	{
	    case DLPSTAT_NOERR:
		/* Make a copy of the sort block */
		if ((sortblock = (ubyte *) malloc(sortblock_len)) == NULL)
		{
			fprintf(stderr, "** Out of memory\n");
			err = -1;
			goto abort;
		}
		memcpy(sortblock, rptr, sortblock_len);
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

	/* XXX - Index header info:
	   nextID (probably safe to set to 0)
	   len - # of resources: DlpOpenDBInfo
	 */

	/* Find out how many resources there are in this database */
	err = DlpReadOpenDBInfo(pconn, dbh, &opendbinfo);
	if (err != DLPSTAT_NOERR)
	{
		err = -1;
		goto abort;
	}
	num_rsrcs = opendbinfo.numrecs;	/* Just for convenience */
printf("  %s has %d resources\n", dbinfo->name, num_rsrcs);

	/** Get the resource IDs **/
	/* Allocate space for the array of resource IDs */
	if (num_rsrcs > 0)
	{
#if 0
		if ((rsrcids = (udword *) calloc(num_rsrcs, sizeof(udword)))
		    == NULL)
		{
			fprintf(stderr, "Out of memory\n");
			err = -1;
			goto abort;
		}
		/* Get the resource IDs */
		/* XXX - Need flags */
		/* XXX - Need const for "everything" */
		err = DlpReadResourceIDList(pconn, dbh, 0x00, 0, ~0,
					  &num, rsrcids);
		if (err != DLPSTAT_NOERR)
		{
			fprintf(stderr, "Can't get array of resource IDs: %d\n",
				err);
			err = -1;
			goto abort;
		}
#endif	/* 0 */

		/* Allocate space to store the resource info structs */
		if ((rsrcinfo = (struct dlp_resource *)
		     calloc(num_rsrcs, sizeof(struct dlp_resource)))
		    == NULL)
		{
			fprintf(stderr, "Out of memory\n");
			err = -1;
			goto abort;
		}

		/* Allocate the array that'll hold pointers to the resource
		 * data.
		 */
		if ((rsrcdata = (ubyte **) calloc(num_rsrcs,
						 sizeof(ubyte *)))
		    == NULL)
		{
			fprintf(stderr, "Out of memory\n");
			err = -1;
			goto abort;
		}

		/* Read the resource info structs, and the resource data */
		for (i = 0; i < num_rsrcs; i++)
		{
/*  extern int slp_debug; */
/*  extern int padp_debug; */

/*  slp_debug = 100; */
/*  padp_debug = 100; */
			/* XXX - Need a const for "everything" */
			err = DlpReadResourceByIndex(
				pconn, dbh,
				i,
				0, ~0,
				&(rsrcinfo[i]),
				&rptr);
			if (err != DLPSTAT_NOERR)
			{
				fprintf(stderr, "Can't read resource\n");
				err = -1;
				goto abort;
			}

			/* Get a copy of the resource data */
			if ((rsrcdata[i] = (ubyte *) malloc(rsrcinfo[i].size))
			    == NULL)
			{
				fprintf(stderr, "Out of memory\n");
				err = -1;
				goto abort;
			}
			memcpy(rsrcdata[i], rptr, rsrcinfo[i].size);
		}
	}
#if 0
	printf("Resource IDs:\n");
	for (i = 0; i < num_rsrcs; i++)
		printf("\t%d: 0x%08lx\n", i, rsrcids[i]);
#endif	/* 0 */

	/* XXX - Resource index:
	   For each resource:
	   - offset: determined by things' size
	   - attributes: DlpReadResourceByID
	   - unique ID: DlpReadResourceByID
	 */

	/* Open the backup file */
	if ((outfd = open(bakfname, O_WRONLY | O_CREAT, 0666)) < 0)
	{
		fprintf(stderr, "Can't open \"%s\" for writing\n",
			bakfname);
		perror("open");
		err = -1;
		goto abort;
	}

	/* Initialize next_offset: the first thing we'll need an offset for
	 * (probably the AppInfo block) will go after the two useless NULs.
	 */
	next_offset = PDB_HEADER_LEN + PDB_RECORDLIST_LEN +
		(num_rsrcs * PDB_RESOURCEIX_LEN) + 2;

	/* Construct the database header */
	wptr = header_buf;
	memcpy(wptr, dbinfo->name, PDB_DBNAMELEN);	/* name */
	wptr += PDB_DBNAMELEN;
	/* Clear the open flag before writing */
	dbinfo->db_flags &= ~DLPCMD_DBFLAG_OPEN;
	put_uword(&wptr, dbinfo->db_flags);		/* attributes */
	put_uword(&wptr, dbinfo->version);		/* version */
	put_udword(&wptr, ctime);			/* ctime */
	put_udword(&wptr, mtime);			/* mtime */
	put_udword(&wptr, baktime);			/* baktime */
	put_udword(&wptr, dbinfo->modnum);		/* modnum */
	if (appblock == NULL)
		put_udword(&wptr, 0L);			/* appinfoID */
	else {
		put_udword(&wptr, next_offset);		/* appinfoID */
		next_offset += appblock_len;	/* Update next_offset */
	}
	if (sortblock == NULL)
{
fprintf(stderr, "no sortblock offset\n");
		put_udword(&wptr, 0L);			/* sortinfoID */
}
	else {
fprintf(stderr, "sortblock's offset: 0x%08lx\n", next_offset);
		put_udword(&wptr, next_offset);		/* sortinfoID */
		next_offset += sortblock_len;	/* Update next_offset */
	}
	put_udword(&wptr, dbinfo->type);		/* type */
	put_udword(&wptr, dbinfo->creator);		/* creator */
	put_udword(&wptr, 0L);				/* uniqueIDseed */

	/* Write the database header */
	if ((err = write(outfd, header_buf, PDB_HEADER_LEN)) != PDB_HEADER_LEN)
	{
		perror("write");
		goto abort;
	}

	/* Construct the resource index header */
	wptr = rl_header_buf;
	put_udword(&wptr, 0L);		/* nextID */
					/* XXX - Should this be something else? */
	put_uword(&wptr, num_rsrcs);	/* len */

	/* Write the resource index header */
	if ((err = write(outfd, rl_header_buf, PDB_RECORDLIST_LEN)) !=
	    PDB_RECORDLIST_LEN)
	{
		perror("write");
		goto abort;
	}

	/* Write the resource index, one resource at a time */
	for (i = 0; i < num_rsrcs; i++)
	{
		wptr = rsrcix_buf;

		put_udword(&wptr, rsrcinfo[i].type);	/* type */
		put_uword(&wptr, rsrcinfo[i].id);	/* id */
		put_udword(&wptr, next_offset);		/* offset */
		next_offset += rsrcinfo[i].size; /* Update next_offset */

		/* Write the resource index entry */
		if ((err = write(outfd, rsrcix_buf, PDB_RESOURCEIX_LEN)) !=
		    PDB_RESOURCEIX_LEN)
		{
			perror("write");
			goto abort;
		}
	}

	/* Write those useless two NULs */
	wptr = header_buf;
	put_uword(&wptr, 0);
	if ((err = write(outfd, header_buf, 2)) < 0)
	{
		perror("write");
		goto abort;
	}

	/* Write the AppInfo block, if there is one */
	if (appblock != NULL)
		if ((err = write(outfd, appblock, appblock_len)) !=
		    appblock_len)
		{
			perror("write");
			goto abort;
		}

	/* Write the sort block, if there is one */
	if (sortblock != NULL)
		if ((err = write(outfd, sortblock, sortblock_len)) !=
		    sortblock_len)
		{
			perror("write");
			goto abort;
		}

	/* Write each resource in turn */
	for (i = 0; i < num_rsrcs; i++)
	{
		if ((err = write(outfd, rsrcdata[i], rsrcinfo[i].size)) !=
		    rsrcinfo[i].size)
		{
			perror("write");
			goto abort;
		}
	}

	/* XXX - I'm not sure about these two, nor their relative order.
	 * HotSync appears to do both at different times.
	 */
/* XXX - It appears to work if you clean the database, only. Anything else
 * causes it to reset.
 */
#if 0
fprintf(stderr, "### Cleaning up database\n");
	err = DlpCleanUpDatabase(pconn, dbh);
	if (err != DLPSTAT_NOERR)
	{
		fprintf(stderr, "Can't clean up database.\n");
		err = -1;
		goto abort;
	}
#endif	/* 0 */

#if 0
fprintf(stderr, "### Resetting sync flags\n");
	err = DlpResetSyncFlags(pconn, dbh);
	if (err != DLPSTAT_NOERR)
	{
		fprintf(stderr, "Can't reset sync flags.\n");
		err = -1;
		goto abort;
	}
#endif	/* 0 */

  abort:	/* In case of error, the code above just jumps to here.
		 * Clean everything up in the reverse order of how it was
		 * initialized.
		 */
	if (outfd >= 0)			/* Close the file */
		close(outfd);

	if (rsrcdata != NULL)		/* It's an array of pointers */
	{
		for (i = 0; i < num_rsrcs; i++)
			if (rsrcdata[i] != NULL)
				free(rsrcdata[i]);
		free(rsrcdata);
	}

	if (rsrcinfo != NULL)
		free(rsrcinfo);
	if (sortblock != NULL)
		free(sortblock);
	if (appblock != NULL)
		free(appblock);

	/* Close the database */
	if (dbh != 0xffff)
		DlpCloseDB(pconn, dbh);	/* Don't really care if this fails */

	return err;
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
