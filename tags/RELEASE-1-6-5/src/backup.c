/* backup.c
 *
 * Functions for backing up Palm databases (both .pdb and .prc) from
 * the Palm to the desktop.
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: backup.c,v 2.28 2001-01-11 08:27:22 arensb Exp $
 */
#include "config.h"
#include <stdio.h>
#include <fcntl.h>		/* For open() */
#include <string.h>		/* For strncpy(), strncat() */
#include <ctype.h>		/* For isprint() */

#if HAVE_LIBINTL_H
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL_H */

#include "pconn/pconn.h"
#include "pdb.h"
#include "coldsync.h"

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

	add_to_log(_("Backup "));
	add_to_log(dbinfo->name);
	add_to_log(" - ");

	SYNC_TRACE(2)
		fprintf(stderr, "Backing up \"%s\" to \"%s\"\n",
			dbinfo->name, bakfname);

	/* Create and open the backup file */
	/* XXX - Is the O_EXCL flag desirable? */
	bakfd = open((const char *) bakfname,
		     O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0600);
	if (bakfd < 0)
	{
		Error(_("%s: can't create new backup file %s.\n"
			"It may already exist.\n"),
		      "backup",
		      bakfname);
		perror("open");
		add_to_log(_("Error\n"));
		return -1;
	}
	/* XXX - Lock the file */

	/* Open the database on the Palm */
	err = DlpOpenConduit(pconn);
	if (err != DLPSTAT_NOERR)
	{
		Error(_("Can't open backup conduit.\n"));
		close(bakfd);
		add_to_log(_("Error\n"));
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
	if (err != DLPSTAT_NOERR)
	{
		Error(_("Can't open database \"%s\".\n"),
		      dbinfo->name);
		close(bakfd);
		add_to_log(_("Error\n"));
		return -1;
	}

	/* Download the database from the Palm */
	pdb = pdb_Download(pconn, dbinfo, dbh);
	if (pdb == NULL)
	{
		/* Error downloading the file.
		 * We don't send an error message to the Palm because
		 * typically the problem is that the connection to the Palm
		 * was lost.
		 */
		err = DlpCloseDB(pconn, dbh);
		unlink(bakfname);	/* Delete the zero-length backup
					 * file */
		close(bakfd);
		add_to_log(_("Error\n"));
		return -1;
	}
	SYNC_TRACE(7)
		fprintf(stderr, "After pdb_Download\n");
	DlpCloseDB(pconn, dbh);
	SYNC_TRACE(7)
		fprintf(stderr, "After DlpCloseDB\n");

	/* Write the database to the backup file */
	err = pdb_Write(pdb, bakfd);
	if (err < 0)
	{
		Error(_("%s: can't write database \"%s\" to \"%s\".\n"),
		      "backup",
		      dbinfo->name, bakfname);
		err = DlpCloseDB(pconn, dbh);
		free_pdb(pdb);
		close(bakfd);
		add_to_log(_("Error\n"));
		return -1;
	}
	SYNC_TRACE(3)
		fprintf(stderr, "Wrote \"%s\" to \"%s\"\n",
			dbinfo->name, bakfname);

	err = DlpCloseDB(pconn, dbh);
	free_pdb(pdb);
	close(bakfd);
	add_to_log(_("OK\n"));
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

	palm_fetch_all_DBs(palm);
	/* XXX - Error-checking */

	palm_resetdb(palm);
	while ((cur_db = palm_nextdb(palm)) != NULL)
	{
		err = backup(pconn, cur_db, backupdir);
		if (err < 0)
		{
			Error(_("%s: Can't back up \"%s\".\n"),
			      "full_backup",
			      cur_db->name);

			/* If the problem is that we've lost the connection
			 * to the Palm, then abort. Otherwise, hope that
			 * the problem was transient, and continue.
			 */
			/* XXX - Ought to make sure that 'palm_errno' is
			 * set.
			 */
			if (palm_errno == PALMERR_TIMEOUT)
				return -1;
		}
	}
	return 0;
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
