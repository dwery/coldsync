/* restore.c
 *
 * Functions for restoring Palm databases (both .pdb and .prc) from
 * the desktop to the Palm.
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: restore.c,v 2.33 2002-09-07 15:08:20 azummo Exp $
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

#if HAVE_LIBINTL_H
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL_H */

#include "pconn/pconn.h"
#include "pdb.h"
#include "coldsync.h"
#include "cs_error.h"

/* restore_file
 * Restore an individual file.
 */
int
restore_file(PConnection *pconn,
	     struct Palm *palm,
	     const char *fname)
{
	int err;
	int bakfd;		/* Backup file descriptor */
	struct pdb *pdb;	/* The backup database */

	Verbose(1, _("Uploading \"%s\""), fname);

	/* Read the PDB file */
	if ((bakfd = open(fname, O_RDONLY | O_BINARY)) < 0)
	{
		Error(_("Can't open %s."),
		      fname);
		Perror("open");
		return -1;
	}

	pdb = pdb_Read(bakfd);
	if (pdb == NULL)
	{
		Error(_("Can't read %s."), fname);
		close(bakfd);
		return -1;
	}

	close(bakfd);

	err = DlpDeleteDB(pconn, CARD0, pdb->name);
		/* XXX - It isn't immediately clear what would be returned
		 * if you tried deleting a read-only database. Try it and
		 * find out.
		 */
		/* It's DLPSTAT_READONLY. Maybe we could check pdb->attributes
		 * to see if the RO flag is set and avoid the call to
		 * DlpDeleteDB()? btw, as is, will allow you to upload
		 * system updates like FATFS.prc .
		 */

	switch ((dlp_stat_t) err)
	{
	    case DLPSTAT_NOERR:
	    case DLPSTAT_NOTFOUND:
		/* If the database wasn't found, that's not an error */
		break;
	    case DLPSTAT_READONLY:
	    	/* The db is read only or ROM based */
	    	break;
	    case DLPSTAT_DBOPEN:
		/* Database is already open by someone else. If the
		 * OKNEWER flag is set, then it's okay to overwrite it
		 * anyway (e.g., "Graffiti Shortcuts").
		 */
	    {
		    const struct dlp_dbinfo *remotedb;

		    /* Look up the database on the Palm. If it has the
		     * OKNEWER flag set, then go ahead and upload the
		     * new database anyway.
		     */
		    remotedb = palm_find_dbentry(palm, pdb->name);
		    if (remotedb == NULL)
		    {
			    /* This should never happen */
			    Warn(_("%s: Database %s doesn't exist, "
				   "yet it is open. Huh?"),
				 "Restore",
				 pdb->name);
			    /* But it shouldn't bother us any */
			    break;
		    }

		    if ((remotedb->db_flags & DLPCMD_DBFLAG_OKNEWER)
			== 0)
		    {
			    Warn(_("%s: Can't restore %s: it is "
				   "opened by another application."),
				 "Restore",
				 pdb->name);
			    return -1;
		    }

		    /* XXX - Even if OKNEWER is set, pdb_Upload() will fail
		     * later on, because it tries to create the database
		     * anew. This fails because the database couldn't be
		     * deleted.
		     * Presumably, the thing to do is to add a special
		     * function for this case, one that deletes all of the
		     * records in the database, and uploads new ones.
		     */

		    break;	/* Okay to overwrite */
	    }

	    default:
		Warn(_("Restore: Can't delete database \"%s\"."),
		     pdb->name);
		print_latest_dlp_error(pconn);
		return -1;
	}

	/* Call pdb_Upload() to install the file. It shouldn't exist
	 * any more by now.
	 */
	err = upload_database(pconn, pdb);
		/* XXX - This appears to fail for "Graffiti Shortcuts": it
		 * tries to create the database on the Palm, but since the
		 * database already exists (it can't be deleted because
		 * it's already open), it fails.
		 */
	SYNC_TRACE(4)
		fprintf(stderr, "pdb_Upload returned %d\n", err);  
	if (err < 0)
	{
		/* XXX - Ugh. Hack-ptui! */
		switch (cs_errno)
		{
		    case CSE_CANCEL:
			va_add_to_log(pconn, "%s %s - %s\n",
				      _("Restore"), pdb->name, _("Cancelled"));
			free_pdb(pdb);
			return -1;
		    case CSE_NOCONN:
			free_pdb(pdb);
			return -1;
		    default:
			/* Anything else, we hope is transient.
			 * Continue and hope for the best.
			 */
			va_add_to_log(pconn, "%s %s - %s\n",
				      _("Restore"), pdb->name, _("Error"));
			break;
		}
	} else
		va_add_to_log(pconn, "%s %s - %s\n",
			      _("Restore"), pdb->name, _("OK"));

	/* Free the PDB */
	free_pdb(pdb);

	return 0;
}

/* restore_dir
 * Restore all of the databases in a directory.
 */
int
restore_dir(PConnection *pconn,
	    struct Palm *palm,
	    const char *dirname)
{
	int err;
	DIR *dir;
	struct dirent *file;
	static char fname[MAXPATHLEN+1];	/* Full pathname of
						 * file to restore */

	MISC_TRACE(1)
		fprintf(stderr, "Restoring from \"%s\"\n",
			dirname == NULL ? "(null)" :
			dirname);

	dir = opendir(dirname);
	if (dir == NULL)
	{
		Error(_("Can't read contents of directory %s."),
		      dirname);
		Perror("opendir");
		return -1;
	}

	/* Look at each file in turn */
	while ((file = readdir(dir)) != NULL)
	{
		SYNC_TRACE(3)
			fprintf(stderr, "Examining %s/%s\n",
				dirname, file->d_name);

		/* Make sure this file has the proper extension for a Palm
		 * database of some sort. If not, ignore it.
		 */
		if (!is_database_name(file->d_name))
		{
			SYNC_TRACE(6)
				fprintf(stderr,
					"Ignoring \"%s\": not a valid "
					"filename extension.\n",
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
		/* XXX - Replace with snprintf() */
		strncpy(fname, dirname, MAXPATHLEN);
		strncat(fname, "/", MAXPATHLEN-strlen(fname));
		strncat(fname, file->d_name, MAXPATHLEN-strlen(fname));

		err = restore_file(pconn, palm, fname);

		if (err < 0 && cs_errno_fatal(cs_errno))
		{
			closedir(dir);
			return -1;
		}
	}

	closedir(dir);

	return 0;
}

/* This is for Emacs's benefit:
 * Local Variables:	***
 * fill-column:	75	***
 * End:			***
 */
