/* backup.c
 *
 * Functions for backing up Palm databases (both .pdb and .prc) from
 * the Palm to the desktop.
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: backup.c,v 2.16 2000-11-04 08:06:59 arensb Exp $
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
backup(struct PConnection *pconn,
       const struct dlp_dbinfo *dbinfo,
       const char *dirname)		/* Directory to back up to */
{
	int err;
	const char *bakfname;		/* Backup pathname */
	int bakfd;			/* Backup file descriptor */
	struct pdb *pdb;		/* Database downloaded from Palm */
	ubyte dbh;			/* Database handle (on Palm) */

	bakfname = mkfname(dirname, dbinfo, True);
				/* Construct the backup file name */

	add_to_log(_("Backup "));
	add_to_log(dbinfo->name);
	add_to_log(" - ");

	SYNC_TRACE(2)
		fprintf(stderr, "Backing up \"%s\" to \"%s\"\n",
			dbinfo->name, bakfname);

	/* Create and open the backup file */
	/* XXX - Is the O_EXCL flag desirable? */
	bakfd = open(bakfname, O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0600);
	if (bakfd < 0)
	{
		fprintf(stderr, _("%s: can't create new backup file "
				  "%s\n"
				  "It may already exist.\n"),
			"Backup",
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
		fprintf(stderr, _("Can't open backup conduit.\n"));
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
		fprintf(stderr, _("Can't open database \"%s\"\n"),
			dbinfo->name);
		close(bakfd);
		add_to_log(_("Error\n"));
		return -1;
	}

	/* Download the database from the Palm */
	pdb = pdb_Download(pconn, dbinfo, dbh);
	SYNC_TRACE(7)
		fprintf(stderr, "After pdb_Download\n");
	DlpCloseDB(pconn, dbh);
	SYNC_TRACE(7)
		fprintf(stderr, "After DlpCloseDB\n");

	/* Write the database to the backup file */
	err = pdb_Write(pdb, bakfd);
	if (err < 0)
	{
		fprintf(stderr, _("%s: can't write database \"%s\" "
				  "to \"%s\"\n"),
			"Backup",
			dbinfo->name, bakfname);
		close(bakfd);
		add_to_log(_("Error\n"));
		return -1;
	}
	SYNC_TRACE(3)
		fprintf(stderr, "Wrote \"%s\" to \"%s\"\n",
			dbinfo->name, bakfname);

	close(bakfd);
	add_to_log(_("OK\n"));
	return 0;
}

/* full_backup
 * Do a full backup of every database on the Palm to
 * 'global_opts.backupdir'.
 */
int
full_backup(struct PConnection *pconn,
	    struct Palm *palm)
{
	int err;
	int i;

	SYNC_TRACE(1)
		fprintf(stderr, "Inside full_backup() -> \"%s\"\n",
			global_opts.backupdir);

	for (i = 0; i < palm->num_dbs; i++)
	{
		err = backup(pconn, &(palm->dblist[i]), global_opts.backupdir);
		if (err < 0)
		{
			fprintf(stderr, "%s: Error backing up \"%s\"\n",
				"full_backup",
				palm->dblist[i].name);
			/* But try to continue anyway */
		}
	}
	return 0;
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
