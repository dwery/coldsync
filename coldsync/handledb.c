/* handledb.c
 *
 * Figure out what to do with a database on the Palm.
 *
 * $Id: handledb.c,v 1.2 1999-02-24 13:19:31 arensb Exp $
 */

#include <stdio.h>
#include <sys/param.h>		/* For MAXPATHLEN */
#include <sys/types.h>		/* For stat() */
#include <sys/stat.h>		/* For stat() */
#include "config.h"
#include "coldsync.h"
#include "pconn/dlp_cmd.h"

#define IS_RSRC_DB(dbinfo)	((dbinfo)->db_flags & DLPCMD_DBFLAG_RESDB)
#define IS_ROM_DB(dbinfo)	((dbinfo)->db_flags & DLPCMD_DBFLAG_RO)

/* Cold_HandleDB

 * Sync database number 'dbnum' with the desktop. If the database doesn't
 * exist on the desktop, 
 */
int
Cold_HandleDB(struct PConnection *pconn,
	      struct ColdPalm *palm,
	      const int dbnum)
{
	int err;
	struct dlp_dbinfo *dbinfo;	/* Info about the database we're
					 * trying to handle. */
	static char bakfname[MAXPATHLEN];
					/* Filename to sync with */
	struct stat statbuf;		/* For finding out whether the
					 * file exists */

	dbinfo = &(palm->dblist[dbnum]);	/* Convenience pointer */

	/* Ignore ROM databases, since there's no point in backing
	 * them up.
	 */
	/* XXX - This behavior should be the default, but optional */
	if (IS_ROM_DB(dbinfo))
	{
printf("\"%s\" is a ROM database. I'm not backing it up.\n",
       dbinfo->name);
		return 0;
	}

	/* Construct the name of the file that this database needs to
	 * be synced with.
	 */
	/* XXX - This needs to be a lot more bullet-proof: get the
	 * real backup directory name, make sure the filename doesn't
	 * have any weird characters in it (like "/").
	 */
#if HAVE_SNPRINTF
	snprintf(bakfname, MAXPATHLEN, "%s/%s.%s",
		 BACKUP_DIR,
		 dbinfo->name,
		 IS_RSRC_DB(dbinfo) ? "prc" : "pdb");
#else
	sprintf(bakfname, "%s/%s.%s",
		 BACKUP_DIR,
		 dbinfo->name,
		 IS_RSRC_DB(dbinfo) ? "prc" : "pdb");
#endif

printf("I want to sync \"%s\" with \"%s\"\n", dbinfo->name, bakfname);

	/* See if the file that we want to sync with exists */
	err = stat(bakfname, &statbuf);
	if (err < 0)
	{
		/* XXX - Ought to check errno and create any missing
		 * directories.
		 */
printf("The file \"%s\" doesn't exist: need to do a backup.\n", bakfname);
		if (IS_RSRC_DB(dbinfo))
		{
			printf("\tNeed to do a resource backup\n");
			return Cold_ResourceBackup(pconn, palm,
						   dbinfo, bakfname);
		} else {
			printf("\tNeed to do a record backup\n");
			return Cold_RecordBackup(pconn, palm,
						   dbinfo, bakfname);
		}
	} else {
printf("The file \"%s\" exists: need to do a sync.\n", bakfname);
		if (IS_RSRC_DB(dbinfo))
		{
			printf("\tNeed to do a resource sync\n");
/* XXX - This ought to be a resource sync, but it's not implemented yet,
 * and I don't want to have to 'rm' leftover files all the time while
 * I'm testing.
 */
return Cold_ResourceBackup(pconn, palm,
			   dbinfo, bakfname);
		} else {
			printf("\tNeed to do a record sync\n");
/* XXX - This ought to be a record sync, but it's not implemented yet,
 * and I don't want to have to 'rm' leftover files all the time while
 * I'm testing.
 */
return Cold_RecordBackup(pconn, palm,
			 dbinfo, bakfname);
		}
	}

	return 0;
}
