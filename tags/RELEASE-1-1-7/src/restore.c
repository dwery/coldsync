/* restore.c
 *
 * Functions for restoring Palm databases (both .pdb and .prc) from
 * the desktop to the Palm.
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: restore.c,v 2.8 2000-03-14 07:08:32 arensb Exp $
 */
#include "config.h"
#include <stdio.h>
#include <fcntl.h>		/* For open() */
#include <string.h>		/* For strlen() and friends */

#if HAVE_STRINGS_H
#  include <strings.h>		/* For strcasecmp() under AIX */
#endif	/* HAVE_STRINGS_H */

#include <sys/types.h>

#if HAVE_DIRENT_H
# include <dirent.h>
# define NAMLEN(dirent) strlen((dirent)->d_name)
#else
# define dirent direct
# define NAMLEN(dirent) (dirent)->d_namlen
# if HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif
# if HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif
# if HAVE_NDIR_H
#  include <ndir.h>
# endif
#endif

#if HAVE_LIBINTL
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL */

#include "pconn/pconn.h"
#include "pdb.h"
#include "coldsync.h"

int
Restore(struct PConnection *pconn,
	struct Palm *palm)		/* XXX - Unused argument */
{
	int err;
	DIR *dir;
	struct dirent *file;
	static char fname[MAXPATHLEN+1];	/* Full pathname of
						 * file to restore */

	MISC_TRACE(1)
		fprintf(stderr, "Restoring from \"%s\"\n",
			global_opts.restoredir == NULL ? "(null)" :
			global_opts.restoredir);

	dir = opendir(global_opts.restoredir);
	if (dir == NULL)
	{
		fprintf(stderr, _("Can't read contents of directory %s\n"),
			global_opts.restoredir);
		perror("opendir");
		return -1;
	}

	/* Look at each file in turn */
	while ((file = readdir(dir)) != NULL)
	{
		char *extptr;		/* Pointer to filename extension */
		int bakfd;		/* Backup file descriptor */
		struct pdb *pdb;	/* The backup database */

		SYNC_TRACE(3)
			fprintf(stderr, "Examining %s/%s\n",
				global_opts.restoredir,
				file->d_name);

/* XXX - This seems to have broken somewhere along the way */
#if 0
/* XXX - Do we really want to check that it's a regular file? Should
 * an intelligent user be able to have symlinks? What about a stupid
 * user?
 */
#if HAVE_DIRENT_TYPE
		/* Make sure this is a regular file */
		if (file->d_type != DT_REG)
		{
			/* Dunno wht it is, but it's not a regular
			 * file. Ignore it.
			 */
			SYNC_TRACE(3)
				fprintf(stderr,
					"  Ignoring %s: not a regular file (%d)\n",
					file->d_name, file->d_type);
			continue;
		}
#else	/* HAVE_DIRENT_TYPE */
		/* XXX - Ought to stat() the file */
#endif	/* !HAVE_DIRENT_TYPE */
#endif	/* 0 */

		/* Make sure the file name ends in ".prc" or ".pdb" */
		extptr = strrchr(file->d_name, '.');
					/* Get the last dot in the name */
		if (extptr == NULL)
		{
			/* This file's name doesn't have an extension */
			SYNC_TRACE(5)
				fprintf(stderr,
					"  Ignoring %s: no extension\n",
					file->d_name);
			continue;
		}

		/* Now that we know that it has an extension, make
		 * sure it's one of the approved ones.
		 */
		if ((strcasecmp(extptr, ".prc") != 0) &&
		    (strcasecmp(extptr, ".pdb") != 0))
		{
			/* Nope. Wrong extension */
			SYNC_TRACE(5)
				fprintf(stderr,
					"  Ignoring %s: bad extension\n",
					file->d_name);
			continue;
		}

		/* Okay, we should restore this file */
		SYNC_TRACE(2)
			fprintf(stderr, "  Uploading \"%s\"\n",
				file->d_name);

		/* Construct the full pathname */
		/* XXX - I don't really like the single "/" in the
		 * middle, but strnprintf() isn't portable. Then
		 * again, is it worth caring about?
		 */
		strncpy(fname, global_opts.restoredir, MAXPATHLEN);
		strncat(fname, "/", MAXPATHLEN-strlen(fname));
		strncat(fname, file->d_name, MAXPATHLEN-strlen(fname));

		/* Read the PDB file */
		if ((bakfd = open(fname, O_RDONLY)) < 0)
		{
			fprintf(stderr, _("Error: Can't open %s\n"),
				fname);
			perror("open");
			return -1;
		}

		pdb = pdb_Read(bakfd);
		if (pdb == NULL)
		{
			fprintf(stderr, _("Error reading %s\n"),
				fname);
			close(bakfd);
			return -1;
		}

		close(bakfd);

		/* XXX - Make sure the database isn't read-only:
		 * either it was originally a ROM database (and is
		 * probably still there), or else the database will be
		 * impossible to delete.
		 * XXX - Then again, maybe the intelligent user
		 * should be allowed to do this.
		 * XXX - Perhaps best solution is to upload if the
		 * user specifies -R flag, and discourage people from
		 * using it.
		 */
		/* XXX - No, the most reasonable thing to do is to
		 * check the database list in 'palm': if the database
		 * already exists, delete it. If it's a read-only
		 * database, presumably it's a ROM database and the
		 * delete will fail, in which case skip the upload.
		 * The question is, is it possible to delete a
		 * read-only database in RAM?
		 */
		SYNC_TRACE(3)
			fprintf(stderr, "Checking read-only attribute\n");
		if (pdb->attributes & PDB_ATTR_RO)
		{
			fprintf(stderr,
				_("\"%s\" is a read-only database. "
				"Not uploading\n"),
				file->d_name);
			continue;
		}

		err = DlpDeleteDB(pconn, CARD0, pdb->name);
		switch (err)
		{
		    case DLPSTAT_NOERR:
		    case DLPSTAT_NOTFOUND:
			/* If the database wasn't found, that's not an
			 * error
			 */
			break;
		    case DLPSTAT_DBOPEN:
			/* Database is already open by someone else.
			 * If the OKNEWER flag is set, then it's okay
			 * to overwrite it anyway (e.g., "Graffiti
			 * Shortcuts").
			 */
		    {
			    struct dlp_dbinfo *remotedb;

			    /* Look up the database on the Palm. If it
			     * has the OKNEWER flag set, then go ahead
			     * and upload the new database anyway.
			     */
			    remotedb = find_dbentry(palm, pdb->name);
			    if (remotedb == NULL)
			    {
				    /* This should never happen */
				    fprintf(stderr,
					    _("%s: Database %s doesn't exist, "
					      "yet it is open. Huh?\n"),
					    "Restore",
					    pdb->name);
				    /* But it shouldn't bother us any */
				    break;
			    }

			    if ((remotedb->db_flags & DLPCMD_DBFLAG_OKNEWER)
				== 0)
			    {
				    fprintf(stderr,
					    _("%s: Can't restore %s: it is "
					      "opened by another application"
					      "\n"),
					    "Restore",
					    pdb->name);
				    continue;
			    }

			    break;	/* Okay to overwrite */
		    }

		    default:
			fprintf(stderr,
				_("Restore: Can't delete database \"%s\"."
				" err == %d\n"),
				pdb->name, err);
			continue;
		}

		/* Call pdb_Upload() to install the file. It shouldn't
		 * exist anymore by now.
		 */
		add_to_log(_("Restore "));
		add_to_log(pdb->name);
		add_to_log(" - ");
		err = pdb_Upload(pconn, pdb);
		SYNC_TRACE(4)
			fprintf(stderr, "pdb_Upload returned %d\n", err);  
		if (err < 0)
			add_to_log(_("Error\n"));
		else
			add_to_log(_("OK\n"));
		/* XXX - If pdb_Upload returned -1 because of a fatal
		 * timeout, don't keep trying to upload the others.
		 */

		/* Free the PDB */
		free_pdb(pdb);
	}

	closedir(dir);

	return 0;
}