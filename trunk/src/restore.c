/* restore.c
 *
 * Functions for restoring Palm databases (both .pdb and .prc) from
 * the desktop to the Palm.
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: restore.c,v 2.36 2003-10-16 15:28:40 azummo Exp $
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

/* is_database_restorable
 * determines if restoring this database would be at all possible
 * or sane.
 * the rules for deciding restorability include attributes and database
 * versions.
 * XXX PalmOS version compatibility would be a real nice thing to check
 * if we had that information.
 */
static Bool
is_database_restorable(PConnection *pconn,
                       struct Palm *palm,
                       struct pdb *pdb)
{
	const struct dlp_dbinfo *dbinfo = NULL;

	SYNC_TRACE(6)
		fprintf(stderr, "evaluating restorability of %s\n", pdb->name);  

	if (pdb->attributes & PDB_ATTR_RO) {
		SYNC_TRACE(6)
			fprintf(stderr, " local database is read-only\n");  
		/* don't even bother if the local database says it's read only. */
		return False;
	}

	dbinfo = palm_find_dbentry(palm, pdb->name);
	if (dbinfo == NULL)
	{
		/* A NULL return means either DLP failed or there's no database
	 	 * on the PDA. Anything more subtle than a dropped connection
		 * has to be handled elsewhere. Fortunately, in the case of a restore,
		 * failure _is_ an option.
		 */
		return PConn_isonline(pconn) ? True : False;
	}

	/* We already checked with the local database, but the read only
	 * flag that really matters is the one on the PDA. If the PDA says it's
	 * read only, we're unlikely to convince it otherwise regardless of what
	 * the local database file says.
	 */
	if (dbinfo->db_flags & DLPCMD_DBFLAG_RO )
	{
		SYNC_TRACE(6)
			fprintf(stderr, " remote database is read-only\n");  
		return False;
	}

	/*
	 * It's always a bad idea to overwrite a with an older version of
	 * a database.
	 */
  	 	 
	if (pdb->version < dbinfo->version)
	{
		SYNC_TRACE(6)
			fprintf(stderr, " version mismatch (local: %d, remote: %d)\n", pdb->version, dbinfo->version);
		return False;
	}

	/* this should never happen, but it's a cheap stupidity shield */
	if (pdb->type != dbinfo->type || pdb->creator != dbinfo->creator)
	{
		SYNC_TRACE(6)
			fprintf(stderr, "creator/type mismatch\n");  
		return False;
	}

	return True;
}

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

	if (!is_database_restorable(pconn,palm,pdb))
	{
		SYNC_TRACE(4)
			fprintf(stderr, "database is not restorable\n");  
		va_add_to_log(pconn, "%s %s - %s\n",
			      _("Restore"), pdb->name, _("Not restorable"));
		return 0;
	}

	/* Call pdb_Upload() to install the file. Enable the force
	 * option so that existing databases that aren't newer than
	 * the one were trying to install are wiped first.
	 */
	err = upload_database(pconn, pdb, True);
	SYNC_TRACE(4)
		fprintf(stderr, "upload_database returned %d\n", err);  
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
