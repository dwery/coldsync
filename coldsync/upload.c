/* upload.c
 *
 * Functions for uploading a Palm database to a Palm.
 *
 * $Id: upload.c,v 1.3 1999-03-11 20:38:04 arensb Exp $
 */
#include <stdio.h>
#include "pconn/PConnection.h"
#include "pconn/dlp_cmd.h"
#include "pdb.h"

int
UploadDatabase(struct PConnection *pconn,
	       const struct pdb *db)
{
int i;
	int err;
	struct dlp_createdbreq newdb;
				/* Argument for creating a database */
	ubyte dbh;		/* Handle for new database */

fprintf(stderr, "UploadDatabase: about to upload \"%s\"\n",
db->name);
if (IS_RSRC_DB(db))
{
#if 0
/* XXX - Be nice to dump this info, but this doesn't work with the new
 * linked list-based 'struct pdb'.
 */
for (i = 0; i < db->numrecs; i++)
fprintf(stderr, "\tResource %d, type '%c%c%c%c'\n",
	i,
       (char) (db->rec_index.rsrc[i].type >> 24) & 0xff,
       (char) (db->rec_index.rsrc[i].type >> 16) & 0xff,
       (char) (db->rec_index.rsrc[i].type >> 8) & 0xff,
       (char) db->rec_index.rsrc[i].type & 0xff);
#endif	/* 0 */
}

	/* Delete the database */
	err = DlpDeleteDB(pconn, 0, db->name);
				/* XXX - Card # shouldn't be hardcoded */
	if ((err != DLPSTAT_NOERR) &&
	    (err != DLPSTAT_NOTFOUND))
	{
		fprintf(stderr, "Can't delete \"%s\", err %d\n",
			db->name, err);
		return -1;
	}

	/* Call OpenConduit to let the Palm know that something's
	 * coming, I guess (I'm just doing what HotSync does, here).
	 */
	err = DlpOpenConduit(pconn);
	if (err != DLPSTAT_NOERR)
	{
		fprintf(stderr, "Can't open conduit for \"%s\", err %d\n",
			db->name, err);
		return -1;
	}

	/* Create the database */
	newdb.creator = db->creator;
	newdb.type = db->type;
	newdb.card = 0;		/* XXX - Shouldn't be hard-coded */
	newdb.flags = db->attributes;
	newdb.version = db->version;
	memcpy(newdb.name, db->name, PDB_DBNAMELEN);

	err = DlpCreateDB(pconn, &newdb, &dbh);
	if (err != DLPSTAT_NOERR)
	{
		fprintf(stderr, "Error creating database: %d\n", err);
		return -1;
	}

	/* Write the AppInfo block, if any */
fprintf(stderr, "## Writing appblock 0x%08lx\n", (long) db->appinfo);
	if (db->appinfo != NULL)
	{
		err = DlpWriteAppBlock(pconn, dbh, 0,
				       db->appinfo_len, db->appinfo);
		if (err < 0)
			return -1;
	}

	/* Write the sort block, if any */
fprintf(stderr, "## Writing sort block 0x%08lx\n", (long) db->sortinfo);
	if (db->sortinfo != NULL)
	{
		err = DlpWriteSortBlock(pconn, dbh, 0,
					db->sortinfo_len, db->sortinfo);
		if (err < 0)
			return -1;
	}

#if 0
/* XXX - Rewrite this whole section */
	/* DlpWriteRecord or DlpWriteResource */
	if (IS_RSRC_DB(db))
	{
		int i;
		struct pdb_resource *rsrc;

		/* It's a resource database */
		for (i = 0; i < db->numrecs; i++)
		{
int j;
fprintf(stderr, "UploadDatabase: i == %d, fd == %d\n", i, pconn->fd);
if (db->header.attributes & PDB_ATTR_RESDB)
{
for (j = 0; j < db->reclist_header.len; j++)
fprintf(stderr, "\tResource %d, type '%c%c%c%c'\n",
	j,
       (char) (db->rec_index.rsrc[j].type >> 24) & 0xff,
       (char) (db->rec_index.rsrc[j].type >> 16) & 0xff,
       (char) (db->rec_index.rsrc[j].type >> 8) & 0xff,
       (char) db->rec_index.rsrc[j].type & 0xff);
}
			err = DlpWriteResource(pconn,
					       dbh,
					       db->rec_index.rsrc[i].type,
					       db->rec_index.rsrc[i].id,
					       db->data_len[i],
					       db->data[i]);
			if (err != DLPSTAT_NOERR)
			{
				/* Close the database */
				err = DlpCloseDB(pconn, dbh);
					/* At this point, we don't much
					 * care if the close fails.
					 */
				return -1;
			}
		}
	} else {
		int i;
		struct dlp_writerec rec;
		udword recid;		/* Returned record ID */

		/* It's a record database */
		for (i = 0; i < db->reclist_header.len; i++)
		{
			/* Construct the record description */
			rec.flags = 0x80;	/* I think 0x80 is mandatory */
			rec.recid = db->rec_index.rec[i].uniqueID;
			rec.attributes = db->rec_index.rec[i].attributes &
				0xf0;
			rec.category = db->rec_index.rec[i].attributes & 0x0f;

			/* Write the record */
			err = DlpWriteRecord(pconn,
					     dbh,
					     &rec,
					     db->data_len[i],
					     db->data[i],
					     &recid);
			if (err != DLPSTAT_NOERR)
			{
				/* Close the database */
				err = DlpCloseDB(pconn, dbh);
					/* At this point, we don't much
					 * care if the close fails.
					 */
				return -1;
			}
		}
	}
#endif	/* 0 */

	/* Close the database */
	err = DlpCloseDB(pconn, dbh);

	return 0;
}
