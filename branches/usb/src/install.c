/* install.c
 *
 * Functions for installing new databases on the Palm.
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: install.c,v 2.7 1999-11-27 05:53:59 arensb Exp $
 */

#include "config.h"
#include <stdio.h>
#include <fcntl.h>		/* For open() */
#include <errno.h>		/* For errno */
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

#if HAVE_LIBINTL
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL */

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
/* XXX It ought to be possible to force an upload of a database. But this
 * should probably be done on a per-database basis.
 */
int
InstallNewFiles(struct PConnection *pconn,
		struct Palm *palm,
		char *newdir,		/* Directory from which to install */
		Bool deletep)		/* Flag: delete after installing? */
{
	int err;
	DIR *dir;
	struct dirent *file;

	MISC_TRACE(1)
		fprintf(stderr, "Installing new databases from \"%s\"\n",
			newdir);
	if ((dir = opendir(newdir)) == NULL)
	{
		fprintf(stderr, _("%s: Can't open install directory\n"),
			"InstallNewFiles");
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
		static char bakfname[MAXPATHLEN+1];
					/* The database's full pathname in
					 * the backup directory.
					 */
		int outfd;		/* File descriptor for writing the
					 * database to the backup
					 * directory.
					 */
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

		/* XXX - Is it worth lstat()ing the file, to make sure it's
		 * a file?
		 */

		/* Construct the file's full pathname */
		strncpy(fname, newdir, MAXPATHLEN);
		strncat(fname, "/", MAXPATHLEN - strlen(fname));
		strncat(fname, file->d_name, MAXPATHLEN - strlen(fname));

		/* Open the file, and load it as a Palm database */
		if ((fd = open(fname, O_RDONLY)) < 0)
		{
			fprintf(stderr, _("%s: Can't open \"%s\"\n"),
				"InstallNewFiles",
				fname);
			continue;
		}

		/* Read the database from the file */
		pdb = pdb_Read(fd);
		if (pdb == NULL)
		{
			fprintf(stderr,
				_("%s: Can't load database \"%s\"\n"),
				"InstallNewFiles",
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
			fprintf(stderr, "InstallNewFiles: Uploading \"%s\"\n",
				pdb->name);

		add_to_log(_("Install "));
		add_to_log(pdb->name);
		add_to_log(" - ");
		if (dbinfo != NULL)
		{
			/* Delete the existing database */
			err = DlpDeleteDB(pconn, CARD0, pdb->name);
			if (err < 0)
			{
				fprintf(stderr,
					_("%s: Error deleting \"%s\"\n"),
					"InstallNewFiles",
					pdb->name);
				add_to_log(_("Error\n"));
				free_pdb(pdb);
				continue;
			}
		}

		err = pdb_Upload(pconn, pdb);
		if (err < 0)
		{
			fprintf(stderr,
				_("%s: Error uploading \"%s\"\n"),
				"InstallNewFiles",
				pdb->name);
			add_to_log(_("Error\n"));
			free_pdb(pdb);
			continue;
		}

		/* Add the newly-uploaded database to the list of databases
		 * in 'palm'.
		 */
		SYNC_TRACE(4)
			fprintf(stderr,
				"InstallNewFiles: see if this db exists\n");
		if (find_dbentry(palm, pdb->name) == NULL)
		{
			/* It doesn't exist yet. Good */
			SYNC_TRACE(4)
				fprintf(stderr, "InstallNewFiles: "
					"appending db to palm->dbinfo\n");
			append_dbentry(palm, pdb);	/* XXX - Error-
							 * checking */
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

		/* Check to see whether this file exists in the backup
		 * directory. If it does, then let the conduit deal with
		 * the newly-uploaded version when the sync continues.
		 * XXX - Actually, it might be better to sync with the
		 * database now, no?
		 * If the database doesn't yet exist in the backup
		 * directory, write it there now.
		 */
		/* Construct the pathname to this database in the backup
		 * directory.
		 */
		strncpy(bakfname, backupdir, MAXPATHLEN);
		strncat(bakfname, "/", MAXPATHLEN - strlen(fname));
		strncat(bakfname, pdb->name, MAXPATHLEN - strlen(fname));
		if (IS_RSRC_DB(pdb))
			strncat(bakfname, ".prc", MAXPATHLEN - strlen(fname));
		else
			strncat(bakfname, ".pdb", MAXPATHLEN - strlen(fname));

		SYNC_TRACE(5)
			fprintf(stderr, "Checking for \"%s\"\n",
				bakfname);

		/* If the file exists already, don't overwrite it */
		err = 0;
		outfd = open(bakfname, O_WRONLY | O_CREAT | O_EXCL, 0600);
		if (outfd < 0)
		{
			if (errno != EEXIST)
			{
				fprintf(stderr, _("Error opening \"%s\"\n"),
					bakfname);
				perror("open");
				add_to_log(_("Problem\n"));
				err = -1;	/* XXX */
			}
			SYNC_TRACE(5)
				fprintf(stderr,
					"\"%s\" already exists, maybe\n",
					bakfname);
		} else {
			/* The file doesn't yet exist. Save the database to
			 * it.
			 */
			SYNC_TRACE(4)
				fprintf(stderr, "Writing \"%s\"\n",
					bakfname);
			err = pdb_Write(pdb, outfd);
			if (err < 0)
				add_to_log(_("Error\n"));
			else
				add_to_log(_("OK\n"));
		}

		/* Delete the newly-uploaded file, if appropriate */
		if (deletep && (err == 0))
		{
			SYNC_TRACE(4)
				fprintf(stderr, "Deleting \"%s\"\n",
					fname);
			err = unlink(fname);
			if (err < 0)
			{
				fprintf(stderr, _("Error deleting \"%s\"\n"),
					fname);
				perror("unlink");
			}
		}

		free_pdb(pdb);

		/* XXX - Walk though config.install_q, looking for
		 * appropriate conduits to invoke. (Or should this be done
		 * earlier, before installing at all?)
		 */
	}

	closedir(dir);

	return 0;		/* XXX */
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
