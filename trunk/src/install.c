/* install.c
 *
 * Functions for installing new databases on the Palm.
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: install.c,v 2.33 2001-09-08 00:22:05 arensb Exp $
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

#if HAVE_LIBINTL_H
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL_H */

#include "coldsync.h"
#include "pdb.h"		/* For pdb_Read() */
#include "cs_error.h"

/* upload_database
 * Upload 'db' to the Palm. This database must not exist (i.e., it's the
 * caller's responsibility to delete it if necessary).
 * When a record is uploaded, the Palm may assign it a new record number.
 * upload_database() records this change in 'db', but it is the caller's
 * responsibility to save this change to the appropriate file, if
 * applicable.
 */
int
upload_database(PConnection *pconn, struct pdb *db)
{
	int err;
	ubyte dbh;			/* Database handle */
	struct dlp_createdbreq newdb;	/* Argument for creating a new
					 * database */

	SYNC_TRACE(1)
		fprintf(stderr, "Uploading \"%s\"\n", db->name);

	/* Call OpenConduit to let the Palm (or the user) know that
	 * something's going on. (Actually, I don't know that that's the
	 * reason. I'm just imitating HotSync, here.
	 */
	err = DlpOpenConduit(pconn);
	switch ((dlp_stat_t) err)
	{
	    case DLPSTAT_NOERR:		/* No error */
		break;
	    case DLPSTAT_CANCEL:	/* There was a pending cancellation
					 * by the user, on the Palm. */
		fprintf(stderr, _("Upload of \"%s\" cancelled by Palm.\n"),
			db->name);
		cs_errno = CSE_CANCEL;
		return -1;
	    default:			/* All other errors */
		fprintf(stderr, _("Can't open conduit for \"%s\": %d.\n"),
			db->name, err);
		cs_errno = CSE_OTHER;
		return -1;
	}

	/* Create the database */
	newdb.creator = db->creator;
	newdb.type = db->type;
	newdb.card = CARD0;
	newdb.flags = db->attributes;
			/* XXX - Is this right? This is voodoo code */
	newdb.version = db->version;
	memcpy(newdb.name, db->name, PDB_DBNAMELEN);

	err = DlpCreateDB(pconn, &newdb, &dbh);
	/* XXX - Check err */
	if (err != (int) DLPSTAT_NOERR)
	{
		fprintf(stderr, _("Error creating database \"%s\": %d.\n"),
			db->name, err);
		return -1;
	}

	/* Upload the AppInfo block, if it exists */
	if (db->appinfo_len > 0)
	{
		SYNC_TRACE(3)
			fprintf(stderr, "Uploading AppInfo block\n");

		err = DlpWriteAppBlock(pconn, dbh,
				       db->appinfo_len,
				       db->appinfo);
		/* XXX - Check err */
		if (err < 0)
			return err;
	}

	/* Upload the sort block, if it exists */
	if (db->sortinfo_len > 0)
	{
		SYNC_TRACE(3)
			fprintf(stderr, "Uploading sort block\n");

		err = DlpWriteSortBlock(pconn, dbh,
					db->sortinfo_len,
					db->sortinfo);
		/* XXX - Check err */
		if (err < 0)
			return err;
	}

	/* Upload each record/resource in turn */
	if (IS_RSRC_DB(db))
	{
		/* It's a resource database */
		struct pdb_resource *rsrc;

		SYNC_TRACE(4)
			fprintf(stderr, "Uploading resources.\n");

		for (rsrc = db->rec_index.rsrc;
		     rsrc != NULL;
		     rsrc = rsrc->next)
		{
			SYNC_TRACE(5)
				fprintf(stderr,
					"Uploading resource 0x%04x\n",
					rsrc->id);

			err = DlpWriteResource(pconn,
					       dbh,
					       rsrc->type,
					       rsrc->id,
					       rsrc->data_len,
					       rsrc->data);

			/* XXX - Check err more thoroughly */
			if (err != (int) DLPSTAT_NOERR)
			{
				/* Close the database */
				err = DlpCloseDB(pconn, dbh);
				return -1;
			}
		}
	} else {
		/* It's a record database */
		struct pdb_record *rec;

		SYNC_TRACE(4)
			fprintf(stderr, "Uploading records.\n");

		for (rec = db->rec_index.rec;
		     rec != NULL;
		     rec = rec->next)
		{
			udword newid;		/* New record ID */

			SYNC_TRACE(5)
				fprintf(stderr,
					"Uploading record 0x%08lx\n",
					rec->id);

			/* XXX - Gross hack to avoid uploading zero-length
			 * records (which shouldn't exist in the first
			 * place).
			 */
			if (rec->data_len == 0)
				continue;

			err = DlpWriteRecord(pconn,
					     dbh,
					     0x80,	/* Mandatory magic */
							/* XXX - Actually,
							 * at some point
							 * DlpWriteRecord
							 * will get fixed
							 * to make sure the
							 * high bit is set,
							 * at which point
							 * this argument
							 * will be allowed
							 * to be 0.
							 */
					     rec->id,
					     rec->flags,
					     rec->category,
					     rec->data_len,
					     rec->data,
					     &newid);

			/* XXX - Check err more thoroughly */
			if (err != (int) DLPSTAT_NOERR)
			{
				/* Close the database */
				err = DlpCloseDB(pconn, dbh);
				return -1;
			}

			/* Update the ID assigned to this record */
			rec->id = newid;
		}
	}

	/* Clean up */
	err = DlpCloseDB(pconn, dbh);
	/* XXX - Check err */
	if (err != (int) DLPSTAT_NOERR)
		return -1;

	return 0;		/* Success */
}

#if 0
/* install_file
 * Upload the database in 'fname', unmodified, to the Palm.
 * Returns 0 if successful, or a negative value in case of error.
 */
int
install_file(PConnection *pconn,
	     struct Palm *palm,
	     const char *fname,		/* Name of file to install */
	     Bool deletep)		/* Flag: delete after installing? */
{
	int err;
	int fd;			/* Database file descriptor */
	struct pdb *pdb;	/* The database */
	struct dlp_dbinfo *dbinfo;
				/* Local information about the database */

	/* Open the file, and load it as a Palm database */
	if ((fd = open(fname, O_RDONLY | O_BINARY)) < 0)
	{
		Error(_("%s: Can't open \"%s\"."),
		      "install_file",
		      fname);
		return -1;
	}

	/* Read the database from the file */
	pdb = pdb_Read(fd);
	if (pdb == NULL)
	{
		Error(_("%s: Can't load database \"%s\"."),
		      "install_file",
		      fname);
		close(fd);
		return -1;
	}
	close(fd);

	/* See if we want to install this database */

	/* See if the database already exists on the Palm */
	dbinfo = palm_find_dbentry(palm, pdb->name);
	if ((dbinfo != NULL) && (!global_opts.force_install))
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
			return -1;
		}
	}
	/* XXX - Before installing, make sure to check the PDB_ATTR_OKNEWER
	 * flag: don't overwrite open databases (typically "Graffiti
	 * Shortcuts") unless it's okay to do so.
	 */
	SYNC_TRACE(5)
		fprintf(stderr, "install_file: Uploading \"%s\"\n",
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
			switch (palm_errno)
			{
			    case PALMERR_TIMEOUT:
				cs_errno = CSE_NOCONN;
				break;
			    default:
				break;
			}

			Error(_("%s: Error deleting \"%s\"."),
			      "install_file",
			      pdb->name);
			add_to_log(_("Error\n"));
			free_pdb(pdb);
			return -1;
		}
	}

	err = upload_database(pconn, pdb);
	if (err < 0)
	{
		switch (palm_errno)
		{
		    case PALMERR_TIMEOUT:
			cs_errno = CSE_NOCONN;
			break;
		    default:
			break;
		}

		Error(_("%s: Error uploading \"%s\"."),
		      "install_file",
		      pdb->name);
		add_to_log(_("Error\n"));
		free_pdb(pdb);
		return -1;
	}

	/* Add the newly-uploaded database to the list of databases
		 * in 'palm'.
		 */
	SYNC_TRACE(4)
		fprintf(stderr,
			"install_file: see if this db exists\n");
	if (palm_find_dbentry(palm, pdb->name) == NULL)
	{
		/* It doesn't exist yet. Good */
		SYNC_TRACE(4)
			fprintf(stderr, "install_file: "
				"appending db to palm->dbinfo\n");
		append_dbentry(palm, pdb);	/* XXX - Error-
						 * checking */
	}

	return 0;
}
#endif	/* 0 */

/* NextInstallFile
 * Read the next valid install database in the .palm/install directory and
 * returns its header info (only -- no data) in the struct dlp_dbinfo
 * provided.  Returns a negative number when there are no more valid
 * databases.
 */
int
NextInstallFile(struct dlp_dbinfo *dbinfo)
{
	int err;
	struct pdb pdb;          /* A scratch database */
	static DIR *dir=NULL;
	struct dirent *file;
	int fd;                 /* Database file descriptor */
	
	if(dir==NULL) 
	{
		if ((dir = opendir(installdir)) == NULL)
		{
			Error(_("%s: Can't open install directory."),
			      "NextInstallFile");
			Perror("opendir");
			return -1;
		}
	}
	
	/* Check each file in the directory in turn */
        while ((file = readdir(dir)) != NULL) {
		static char fname[MAXPATHLEN+1];

		/* Does this look like a database? */
		if (!is_database_name(file->d_name))
			continue;	/* No. Ignore it */

		/* Construct the file's full pathname */
		snprintf(fname, MAXPATHLEN,
			 "%s/%s", installdir, file->d_name);

		/* Open the file */
                if ((fd = open(fname, O_RDONLY | O_BINARY)) < 0)
                {
                        Warn(_("%s: Can't open \"%s\"."),
			     "NextInstallFile",
			     fname);
                        continue;
                }
		
		/* Load its header a Palm database */
		if ((err = pdb_LoadHeader(fd, &pdb)) < 0)
		{
			Error(_("Can't load header."));
			continue;
		}
		
		/* Fill in the dbinfo from the pdb header information */
		dbinfo_fill(dbinfo,&pdb);
		return 0;  /* Success */
	}

	/* If we got here... no more valid files available */
	closedir(dir);

	return -1;
}	

/* InstallNewFiles
 * Go through the install directory. If there are any databases there
 * that don't exist on the Palm, install them.
 */
int
InstallNewFiles(PConnection *pconn,
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
		Error(_("%s: Can't open install directory."),
		      "InstallNewFiles");
		Perror("opendir");
		return -1;
	}

	/* Check each file in the directory in turn */
	while ((file = readdir(dir)) != NULL)
	{
		int fd;			/* Database file descriptor */
		struct pdb *pdb;	/* The database */
		static char fname[MAXPATHLEN+1];
					/* The database's full pathname */
		const char *bakfname;	/* The database's full pathname in
					 * the backup directory.
					 */
		int outfd;		/* File descriptor for writing the
					 * database to the backup
					 * directory.
					 */
		const struct dlp_dbinfo *dbinfo;
					/* Local information about the
					 * database
					 */

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

		/* XXX - Is it worth lstat()ing the file, to make sure it's
		 * a file?
		 */

		/* Construct the file's full pathname */
		snprintf(fname, MAXPATHLEN, "%s/%s", newdir, file->d_name);

		/* Open the file, and load it as a Palm database */
		if ((fd = open(fname, O_RDONLY | O_BINARY)) < 0)
		{
			Error(_("%s: Can't open \"%s\"."),
			      "InstallNewFiles",
			      fname);
			continue;
		}

		/* Read the database from the file */
		pdb = pdb_Read(fd);
		if (pdb == NULL)
		{
			Error(_("%s: Can't load database \"%s\"."),
			      "InstallNewFiles",
			      fname);
			close(fd);
			continue;
		}
		close(fd);

		/* See if we want to install this database */

		/* See if the database already exists on the Palm */
		dbinfo = palm_find_dbentry(palm, pdb->name);
		if ((dbinfo != NULL) && (!global_opts.force_install))
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
				Error(_("%s: Error deleting \"%s\"."),
				      "InstallNewFiles",
				      pdb->name);
				add_to_log(_("Error\n"));
				free_pdb(pdb);

				switch (palm_errno)
				{
				    case PALMERR_TIMEOUT:
					cs_errno = CSE_NOCONN;
					return -1;
				    default:
					continue;
				}
			}
		}

		err = upload_database(pconn, pdb);
		if (err < 0)
		{
			switch (palm_errno)
			{
			    case PALMERR_TIMEOUT:
				cs_errno = CSE_NOCONN;
				break;
			    default:
				break;
			}

			Error(_("%s: Error uploading \"%s\"."),
			      "InstallNewFiles",
			      pdb->name);
			add_to_log(_("Error\n"));

			/* XXX - This is rather ugly, due in part to the
			 * fact that pdb_Upload (upload_database) isn't in
			 * the main ColdSync code (yet).
			 */
			/* XXX - So now that it is, what's to be done about
			 * it?
			 */
			switch (cs_errno)
			{
				/* Fatal errors that we know of */
			    case CSE_CANCEL:
			    case CSE_NOCONN:
				free_pdb(pdb);
				return -1;

				/* All other errors */
			    default:
				free_pdb(pdb);
				continue;
			}
		}

		/* Add the newly-uploaded database to the list of databases
		 * in 'palm'.
		 */
		SYNC_TRACE(4)
			fprintf(stderr,
				"InstallNewFiles: see if this db exists\n");
		if (palm_find_dbentry(palm, pdb->name) == NULL)
		{
			/* It doesn't exist yet. Good */
			SYNC_TRACE(4)
				fprintf(stderr, "InstallNewFiles: "
					"appending db to palm->dbinfo\n");

			if (palm_append_dbentry(palm, pdb) < 0)
			{
				free_pdb(pdb);
				return -1;
			}
		}

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
		{
			struct dlp_dbinfo dummy;
				/* Gross hack. mkbakfname() is very
				 * convenient, but takes a struct
				 * dlp_dbinfo. Use a dummy with the
				 * relevant fields filled in.
				 */

			strncpy(dummy.name, pdb->name, DLPCMD_DBNAME_LEN);
			dummy.db_flags = pdb->attributes;
			bakfname = mkbakfname(&dummy);
		}

		SYNC_TRACE(5)
			fprintf(stderr, "Checking for \"%s\"\n",
				bakfname);

		/* If the file exists already, don't overwrite it */
		err = 0;
		outfd = open((const char *) bakfname,
			     O_WRONLY | O_CREAT | O_EXCL | O_BINARY,
			     0600);
		if (outfd < 0)
		{
			if (errno == EEXIST)
			{
				/* File already exists. This isn't a problem */
				add_to_log(_("OK\n"));
			} else {
				Error(_("Error opening \"%s\"."),
				      bakfname);
				Perror("open");
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

		/* XXX - Run Install conduits:
		 * err = run_Install_conduits(pdb);
		 */

		/* Delete the newly-uploaded file, if appropriate */
		if (deletep && (err == 0))
		{
			SYNC_TRACE(4)
				fprintf(stderr, "Deleting \"%s\"\n",
					fname);
			err = unlink(fname);
			if (err < 0)
			{
				Warn(_("Error deleting \"%s\"."),
				     fname);
				Perror("unlink");
			}
		}

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
