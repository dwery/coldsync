/* install.c
 *
 * Functions for installing new databases on the Palm.
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: install.c,v 2.1 1999-09-04 21:05:41 arensb Exp $
 */

#include "config.h"
#include <stdio.h>
#include <fcntl.h>		/* For open() */
#include <sys/types.h>

#if HAVE_DIRENT_H
# include <dirent.h>		/* For readdir() and friends */
# define NAMLEN(dirent) strlen((dirent)->d_name)
#else
# define dirent direct
# define NAMLEN(dirent) (dirent)->d_namlen
# if HAVE_SYS_NDIR_H
#  include <sys/ndir.h>		/* For readdir() and friends */
# endif
# if HAVE_SYS_DIR_H
#  include <sys/dir.h>		/* For readdir() and friends */
# endif
# if HAVE_NDIR_H
#  include <ndir.h>		/* For readdir() and friends */
# endif
#endif

#include <string.h>		/* For strrchr() */

#if HAVE_STRINGS_H
#  include <strings.h>		/* For strcasecmp() under AIX */
#endif	/* HAVE_STRINGS_H */

#include "coldsync.h"
#include "pdb.h"		/* For pdb_Read() */

#if !HAVE_STRCASECMP
#  define	strcasecmp(s1,s2)	strcmp((s1),(s2))
#endif	/* HAVE_STRCASECMP */
#if !HAVE_STRNCASECMP
#  define	strncasecmp(s1,s2,len)	strncmp((s1),(s2),(len))
#endif	/* HAVE_STRNCASECMP */

/* InstallNewFiles
 * Go through the install directory. If there are any databases there
 * that don't exist on the Palm, install them.
 */
/* XXX - Trace statements */
/* XXX - Add an argument specifying which directory to look for files in */
/* XXX - Add an argument to say whether or not to delete each file after
 * consideration. Actually, it'd probably be best not to use rename() for
 * this: we need to pdb_Read() the file in any case; so we should be able
 * to feed this 'struct pdb' to FirstSync() or something, tell it to do
 * something sane with it (in particular, save a copy in 'backupdir'), and
 * then delete the copy in 'installdir'.
 */
/* XXX It ought to be possible to force an upload of a database. But this
 * should probably be done on a per-database basis.
 */
int
InstallNewFiles(struct PConnection *pconn,
		struct Palm *palm)
{
	int err;
	DIR *dir;
	struct dirent *file;

	MISC_TRACE(1)
		fprintf(stderr, "Installing new databases\n");
	if ((dir = opendir(installdir)) == NULL)
	{
		fprintf(stderr, "InstallNewFiles: Can't open install directory\n");
		perror("opendir");
	}

	/* Check each file in the directory in turn */
	while ((file = readdir(dir)) != NULL)
	{
		char *lastdot;		/* Pointer to last dot in filename */
		int fd;			/* Database file descriptor */
		struct pdb *pdb;	/* The database */
		static char fname[MAXPATHLEN+1];
					/* The database's full pathname */
		struct dlp_dbinfo *dbinfo;
					/* Local information about the
					 * database
					 */

		/* Find the last dot in the filename, to see if the
		 * file has a kosher extension (".pdb" or ".prc");
		 */
		lastdot = strrchr(file->d_name, '.');

		if (lastdot == NULL)
			/* No dot, so no extension. Ignore this */
			continue;

		if ((strcasecmp(lastdot, ".pdb") != 0) &&
		    (strcasecmp(lastdot, ".prc") != 0))
			/* The file has an unknown extension. Ignore it */
			continue;

		/* XXX - Is it worth stat()ing the file, to make sure it's
		 * a file?
		 */

		/* Construct the file's full pathname */
		strncpy(fname, installdir, MAXPATHLEN);
		strncat(fname, "/", MAXPATHLEN - strlen(fname));
		strncat(fname, file->d_name, MAXPATHLEN - strlen(fname));

		/* Open the file, and load it as a Palm database */
		if ((fd = open(fname, O_RDONLY)) < 0)
		{
			fprintf(stderr, "InstallNewFiles: Can't open \"%s\"\n",
				fname);
			continue;
		}

		/* Read the database from the file */
		pdb = pdb_Read(fd);
		if (pdb == NULL)
		{
			fprintf(stderr,
				"InstallNewFiles: Can't load database \"%s\"\n",
				fname);
			close(fd);
			continue;
		}
		close(fd);

		/* See if we want to install this database */

		/* See if the database already exists on the Palm */
		dbinfo = find_dbentry(palm, pdb->name);
		if (dbinfo != NULL)
		{
			/* The database exists. Check its modification
			 * number: if it's more recent than the version
			 * currently installed on the Palm, delete the old
			 * version and install the new one.
			 */
			SYNC_TRACE(4)
				fprintf(stderr,
					"Database \"%s\" already exists\n",
					pdb->name);
			SYNC_TRACE(5)
			{
				fprintf(stderr, "  Existing modnum:   %ld\n",
					dbinfo->modnum);
				fprintf(stderr, "  New file's modnum: %ld\n",
					pdb->modnum);
			}

			if (pdb->modnum <= dbinfo->modnum)
			{
				SYNC_TRACE(4)
					fprintf(stderr, "This isn't a new version\n");
				free_pdb(pdb);
				continue;
			}
		}
		/* XXX - Before installing, make sure to check the
		 * PDB_ATTR_OKNEWER flag: don't overwrite open databases
		 * (typically "Graffiti Shortcuts") unless it's okay to do
		 * so.
		 */
		SYNC_TRACE(5)
			fprintf(stderr, "Uploading \"%s\"\n",
				pdb->name);

		if (dbinfo != NULL)
		{
			/* Delete the existing database */
			err = DlpDeleteDB(pconn, CARD0, pdb->name);
			if (err < 0)
			{
				fprintf(stderr,
					"InstallNewFiles: Error deleting \"%s\"\n",
					pdb->name);
				free_pdb(pdb);
				continue;
			}
		}

		err = pdb_Upload(pconn, pdb);
		if (err < 0)
		{
			fprintf(stderr,
				"InstallNewFiles: Error uploading \"%s\"\n",
				pdb->name);
		}

		/* XXX - After installing:
		 * Ideally, instead of installing at all, we should find a
		 * conduit for this database and tell it, "here's a new
		 * database. Do something intelligent with it."
		 * In the meantime, move (rename()) the database to the
		 * backup directory, but only if it doesn't exist there
		 * already. That way, we save the time and effort of
		 * downloading the database immediately after uploading it.
		 * This saves user time.
		 */
		/* XXX - Don't forget to add the new file to palm->dblist
		 * (and update palm->num_dbs) so that the newly-installed
		 * file doesn't immediately get moved to the attic.
		 */
		/* XXX - Add a message to the sync log */

		free_pdb(pdb);
	}

	closedir(dir);

	return 0;		/* XXX */
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
