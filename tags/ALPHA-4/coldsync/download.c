/* download.c
 *
 * Functions to download an entire database from the Palm.
 *
 * $Id: download.c,v 1.1 1999-03-11 03:29:05 arensb Exp $
 */
#include <stdio.h>
#include "pconn/PConnection.h"
#include "pconn/util.h"
#include "pdb.h"
#include "coldsync.h"

struct pdb *
DownloadRecordDB(struct PConnection *pconn,
		 struct dlp_dbinfo *dbinfo)
{
	int err;
	struct pdb *retval;
	ubyte dbh;		/* Database handle */

	/* Allocate the pdb to return */
	if ((retval = new_pdb()) == NULL)
	{
		fprintf(stderr, "DownloadRecordDB: Can't allocate pdb\n");
		return NULL;
	}

	/* Open the database */
	/* XXX - Put the card number in dbinfo? */
	err = DlpOpenDB(pconn, 0,	/* XXX - Card # shouldn't be hardcoded */
			dbinfo->name,
			DLPCMD_MODE_READ |
			DLPCMD_MODE_WRITE |
			DLPCMD_MODE_SECRET,
			&dbh);
	if (err != DLPSTAT_NOERR)
	{
		fprintf(stderr, "### Error opening %s: %d\n",
			dbinfo->name, err);
		free_pdb(retval);
		return NULL;
	}

	/* Convert the times from DLP time structures to Palm-style
	 * time_ts.
	 */
	retval->header.ctime = time_dlp2palmtime(&dbinfo->ctime);
	retval->header.mtime = time_dlp2palmtime(&dbinfo->mtime);
	retval->header.baktime = time_dlp2palmtime(&dbinfo->baktime);

return NULL;
}
