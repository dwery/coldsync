/* coldsync.c
 *
 * $Id: coldsync.c,v 1.6 1999-03-11 04:14:05 arensb Exp $
 */
#include <stdio.h>
#include <fcntl.h>		/* For open() */
#include <stdlib.h>		/* For malloc() */
#include <sys/param.h>		/* For MAXPATHLEN */
#include <sys/types.h>		/* For stat() */
#include <sys/stat.h>		/* For stat() */
#include <termios.h>		/* Experimental */
#include <dirent.h>		/* For opendir(), readdir(), closedir() */
#include <string.h>		/* For strrchr() */
#include <unistd.h>		/* For sleep() */
#include "config.h"
#include "pconn/palm_errno.h"
#include <pconn/PConnection.h>
#include <pconn/cmp.h>
#include <pconn/dlp_cmd.h>
#include <pconn/util.h>
#include "coldsync.h"
#include "pdb.h"

/* XXX - This should be defined elsewhere (e.g., in a config file). The
 * reason there are two macros here is that under Solaris, B19200 != 19200
 * for whatever reason. There should be a table that maps one to the other.
 */
#define SYNC_RATE		57600
#define BSYNC_RATE		B57600

extern int load_config(int argc, char *argv[]);
extern int load_palm_config(struct Palm *palm);
int listlocalfiles(struct PConnection *pconn);
int GetPalmInfo(struct PConnection *pconn, struct Palm *palm);

struct Palm palm;

int
main(int argc, char *argv[])
{
	struct PConnection *pconn;
	int err;
	int i;

	/* Parse arguments */
	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s device\n", argv[0]);
		exit(1);
	}

	/* Read config files */
	if ((err = load_config(argc, argv)) < 0)
	{
		/* XXX - Clean up? */
		exit(1);
	}

	/* XXX - In the production version (daemon), this should just set
	 * up the serial port normally (raw, 9600 bps), then wait for it to
	 * become readable. Then fork, and let the child establish the
	 * connection and sync.
	 */
	/* XXX - Figure out fastest speed at which each serial port will
	 * run
	 */
	if ((pconn = new_PConnection(argv[1])) == NULL)
	{
		fprintf(stderr, "Error: can't open connection.\n");
		/* XXX - Clean up */
		exit(1);
	}

	/* Connect to the Palm */
	if ((err = Connect(pconn, argv[1])) < 0)
	{
		fprintf(stderr, "Can't connect to Palm\n");
		/* XXX - Clean up */
		exit(1);
	}

	/* Get system, NetSync and user info */
	if ((err = GetPalmInfo(pconn, &palm)) < 0)
	{
		fprintf(stderr, "Can't get system/user/NetSync info\n");
		/* XXX - Clean up */
		exit(1);
	}

	/* Figure out which Palm we're dealing with, and load per-palm
	 * config.
	 */
	if ((err = load_palm_config(&palm)) < 0)
	{
		fprintf(stderr, "Can't get per-Palm config.\n");
		/* XXX - Clean up */
		exit(1);
	}

	if ((err = GetMemInfo(pconn, &palm)) < 0)
	{
		fprintf(stderr, "GetMemInfo() returned %d\n", err);
		/* XXX - Clean up */
		exit(1);
	}

	/* XXX - Get list of local files */
/*  listlocalfiles(pconn); */

	/* Get a list of all databases on the Palm */
	if ((err = ListDBs(pconn, &palm)) < 0)
	{
		fprintf(stderr, "ListDBs returned %d\n", err);
		/* XXX - Clean up */
		exit(1);
	}

	/* Print out the list of databases, for posterity */
	/* XXX - Should do this only if debugging turned on */
	printf("\nDatabase list:\n");
	printf("Name                            flags type crea ver mod. num\n"
	       "        ctime                mtime                baktime\n");
	for (i = 0; i < palm.num_dbs; i++)
	{
		printf("%-*s %04x %c%c%c%c %c%c%c%c %3d %08lx\n",
		       PDB_DBNAMELEN,
		       palm.dblist[i].name,
		       palm.dblist[i].db_flags,
		       (char) (palm.dblist[i].type >> 24),
		       (char) (palm.dblist[i].type >> 16),
		       (char) (palm.dblist[i].type >> 8),
		       (char) palm.dblist[i].type,
		       (char) (palm.dblist[i].creator >> 24),
		       (char) (palm.dblist[i].creator >> 16),
		       (char) (palm.dblist[i].creator >> 8),
		       (char) palm.dblist[i].creator,
		       palm.dblist[i].version,
		       palm.dblist[i].modnum);
		printf("        "
		       "%02d:%02d:%02d %02d/%02d/%02d  "
		       "%02d:%02d:%02d %02d/%02d/%02d  "
		       "%02d:%02d:%02d %02d/%02d/%02d\n",
		       palm.dblist[i].ctime.hour,
		       palm.dblist[i].ctime.minute,
		       palm.dblist[i].ctime.second,
		       palm.dblist[i].ctime.day,
		       palm.dblist[i].ctime.month,
		       palm.dblist[i].ctime.year,
		       palm.dblist[i].mtime.hour,
		       palm.dblist[i].mtime.minute,
		       palm.dblist[i].mtime.second,
		       palm.dblist[i].mtime.day,
		       palm.dblist[i].mtime.month,
		       palm.dblist[i].mtime.year,
		       palm.dblist[i].baktime.hour,
		       palm.dblist[i].baktime.minute,
		       palm.dblist[i].baktime.second,
		       palm.dblist[i].baktime.day,
		       palm.dblist[i].baktime.month,
		       palm.dblist[i].baktime.year);
	}

	/* XXX - Figure out which conduits exist for each of the existing
	 * databases; if there are multiple conduits for a given database,
	 * figure out which one to use; if it can be "primed", do so.
	 */

	/* XXX - If it's configured to install new databases first, install
	 * new databases now.
	 */

	/* Synchronize the databases */
	for (i = 0; i < palm.num_dbs; i++)
	{
		err = HandleDB(pconn, &palm, i);
		if (err < 0)
			/* XXX - Error-handling */
			exit(1);
	}

	/* XXX - If it's configured to install new databases last, install
	 * new databases now.
	 */

	/* XXX - Write updated NetSync info */
	/* XXX - Write updated user info */

	/* Finally, close the connection */
	if ((err = Disconnect(pconn, DLPCMD_SYNCEND_NORMAL)) < 0)
	{
		fprintf(stderr, "Error disconnecting\n");
		/* XXX - Clean up */
		exit(1);
	}

	/* XXX - Clean up */
	exit(0);
}

/* Connect
 * Wait for a Palm to show up on the other end.
 */
int
Connect(struct PConnection *pconn/*int fd*/,
	  const char *name)
{
	int err;
	struct slp_addr pcaddr;
	struct cmp_packet cmpp;
	struct termios term;

	pcaddr.protocol = SLP_PKTTYPE_PAD;	/* XXX - This ought to be
						 * part of the initial
						 * socket setup.
						 */
	pcaddr.port = SLP_PORT_DLP;
	PConn_bind(pconn, &pcaddr);
	/* XXX - PConn_accept(fd) */

	printf("===== Waiting for wakeup packet\n");
	do {
		err = cmp_read(pconn, &cmpp);
		if (err < 0)
		{
			if (palm_errno == PALMERR_TIMEOUT)
				continue;
			fprintf(stderr, "Error during cmp_read: (%d) %s\n",
				palm_errno,
				palm_errlist[palm_errno]);
			exit(1); /* XXX */
		}
	} while (cmpp.type != CMP_TYPE_WAKEUP);

	printf("===== Got a wakeup packet\n");
	/* Compose a reply */
	cmpp.type = CMP_TYPE_INIT;
	cmpp.ver_major = 1;	/* XXX - Should be constants in header file */
	cmpp.ver_minor = 1;
	cmpp.rate = SYNC_RATE;
	cmpp.flags = CMP_IFLAG_CHANGERATE;
	printf("===== Sending INIT packet\n");
	cmp_write(pconn, &cmpp);
	printf("===== Finished sending INIT packet\n");

	/* Change the speed */
	/* XXX - This probably goes in Pconn_accept() or something */
	tcgetattr(pconn->fd, &term);
	cfsetspeed(&term, BSYNC_RATE);
	tcsetattr(pconn->fd, TCSANOW, &term);
	sleep(1);		/* XXX - Why is this necessary? (under
				 * FreeBSD 3.0).
				 */

	return 0;
}

int
Disconnect(struct PConnection *pconn/*int fd*/, const ubyte status)
{
	int err;

	/* Terminate the sync */
	err = DlpEndOfSync(pconn, status/*DLPCMD_SYNCEND_NORMAL*/);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpEndOfSync: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		return err;
	}
	fprintf(stderr, "===== Finished syncing\n");

	PConnClose(pconn);		/* Close the connection */

	return 0;
}

/* GetMemInfo
 * Get info about the memory on the Palm and record it in 'palm'.
 */
int
GetMemInfo(struct PConnection *pconn,
		struct Palm *palm)
{
	int err;
	ubyte last_card;
	ubyte more;

	/* Allocate space for the card info */
	if ((palm->cardinfo = (struct dlp_cardinfo *)
	     malloc(sizeof(struct dlp_cardinfo))) == NULL)
		return -1;

	/* Ask the Palm about each memory card in turn */
	/* XXX - Actually, it doesn't: it just asks about memory card 0;
	 * the 'more' return value should be non-zero if there are more
	 * cards to be read, but it's always one on every Palm I've tried
	 * this on.
	 */
	if ((err = DlpReadStorageInfo(pconn, 0, &last_card, &more,
				      palm->cardinfo)) < 0)
		return -1;

	palm->num_cards = 1;	/* XXX - Hard-wired, for the reasons above */

/*
fprintf(stderr, "===== Got memory info:\n");
fprintf(stderr, "\tTotal size:\t%d\n", palm->cardinfo[0].totalsize);
fprintf(stderr, "\tCard number:\t%d\n", palm->cardinfo[0].cardno);
fprintf(stderr, "\tCard version: %d (0x%02x)\n",
	palm->cardinfo[0].cardversion,
	palm->cardinfo[0].cardversion);
fprintf(stderr, "\tCreation time: %02d:%02d:%02d %d/%d/%d\n",
	palm->cardinfo[0].ctime.second,
	palm->cardinfo[0].ctime.minute,
	palm->cardinfo[0].ctime.hour,
	palm->cardinfo[0].ctime.day,
	palm->cardinfo[0].ctime.month,
	palm->cardinfo[0].ctime.year);
fprintf(stderr, "\tROM: %ld (0x%04lx)\n",
	palm->cardinfo[0].rom_size,
	palm->cardinfo[0].rom_size);
fprintf(stderr, "\tRAM: %ld (0x%04lx)\n",
	palm->cardinfo[0].ram_size,
	palm->cardinfo[0].ram_size);
fprintf(stderr, "\tFree RAM: %ld (0x%04lx)\n",
	palm->cardinfo[0].free_ram,
	palm->cardinfo[0].free_ram);
fprintf(stderr, "\tCard name (%d) \"%s\"\n",
	palm->cardinfo[0].cardname_size,
	palm->cardinfo[0].cardname);
fprintf(stderr, "\tManufacturer name (%d) \"%s\"\n",
	palm->cardinfo[0].manufname_size,
	palm->cardinfo[0].manufname);
*/

/* XXX - Check DLP version */
/*  fprintf(stderr, "\tROM databases: %d\n", palm->cardinfo[0].rom_dbs); */
/*  fprintf(stderr, "\tRAM databases: %d\n", palm->cardinfo[0].ram_dbs); */

	return 0;
}

/* ListDBs
 * Fetch the list of database info records from the Palm, both for ROM and
 * RAM.
 * This function must be called after GetMemInfo().
 */
int
ListDBs(struct PConnection *pconn, struct Palm *palm)
{
	int err;
	int card;		/* Memory card number */

	/* Iterate over each memory card */
	for (card = 0; card < palm->num_cards; card++)
	{
		int i;
		ubyte iflags;		/* ReadDBList flags */
		uword start;		/* Database index to start reading
					 * at */
		uword last_index;	/* Index of last database read */
		ubyte oflags;
		ubyte num;

		/* Get total # of databases */
		palm->num_dbs = palm->cardinfo[card].rom_dbs +
			palm->cardinfo[card].ram_dbs;
		if (palm->num_dbs <= 0)
		{
			fprintf(stderr,
				"### Error: you have an old Palm, one that doesn't say how many\n"
				"databases it has. I can't cope with this.\n");
			return -1;
		}

		/* Allocate space for the array of database info blocks */
		if ((palm->dblist = (struct dlp_dbinfo *)
		     calloc(palm->num_dbs, sizeof(struct dlp_dbinfo)))
		    == NULL)
			return -1;

		iflags = DLPCMD_READDBLFLAG_RAM |
			DLPCMD_READDBLFLAG_ROM;
				/* Flags: read both ROM and RAM databases */

		start = 0;	/* Index at which to start reading */

		/* Collect each database in turn. */
		/* XXX - Should handle older devices that don't return the
		 * number of databases.
		 */
		for (i = 0; i < palm->num_dbs; i++)
		{
			err = DlpReadDBList(pconn, iflags, card, start,
					    &last_index, &oflags, &num,
					    &(palm->dblist[i]));
			/* XXX - Until we can get the DLP status code from
			 * palm_errno, we'll need to ignore 'err'.
			 */
			if (err < 0)
				return -1;

			/* Sanity check: if there are no more databases to
			 * be read, stop reading now. This shouldn't
			 * happen, but you never know.
			 */
			if ((oflags & 0x80) == 0)	/* XXX - Need const */
				/* There are no more databases */
				break;

			/* For the next iteration, set the start index to
			 * the index of the database just read, plus one.
			 */
			start = last_index + 1;
			start = last_index+1; 
		}
	}

	return 0;
}

#if 0
int
listlocalfiles(struct PConnection *pconn)
{
	int err;
	DIR *dir;
	struct dirent *file;
	char *lastdot;
	struct pdb *db;
char fnamebuf[MAXPATHLEN];

printf("Listing directory \"%s\"\n", INSTALL_DIR);
	dir = opendir(INSTALL_DIR);
	if (dir == NULL)
	{
		perror("opendir");
		exit(1);
	}

	while ((file = readdir(dir)) != NULL)
	{
		lastdot = strrchr(file->d_name, '.');
		if (lastdot == NULL)
		{
			continue;
		}
		if (strcasecmp(lastdot, ".pdb") == 0)
		{
			/* Do nothing. Fall through */
		} else if (strcasecmp(lastdot, ".prc") == 0)
		{
			/* Do nothing. Fall through */
		} else {
			printf("\tI don't know this file type\n");
			continue;
		}
/* XXX - Possible buffer overflow */
sprintf(fnamebuf, "%s/%s", INSTALL_DIR, file->d_name);
db = pdb_Read(fnamebuf);
err = UploadDatabase(pconn, db);
fprintf(stderr, "UploadDatabase returned %d\n", err);
if (err != 0) return err;
	}
	return 0;
}
#endif	/* 0 */

/* GetUserSysInfo
 * Get system and user info from the Palm, and put it in 'palm'.
 */
int
GetPalmInfo(struct PConnection *pconn,
	       struct Palm *palm)
{
	int err;

	/* Get system information about the Palm */
	if ((err = DlpReadSysInfo(pconn, &(palm->sysinfo))) < 0)
	{
		fprintf(stderr, "Can't get system info\n");
		return -1;
	}
printf("System info:\n");
printf("\tROM version: 0x%08lx\n", palm->sysinfo.rom_version);
printf("\tLocalization: 0x%08lx\n", palm->sysinfo.localization);
printf("\tproduct ID: 0x%08lx\n", palm->sysinfo.prodID);

	/* Get NetSync information from the Palm */
	/* XXX - Need to check some stuff, first: if it's a pre-1.1 Palm,
	 * it doesn't know about NetSync. HotSync tries to
	 * OpenDB("NetSync"), then it tries ReadFeature('netl',0), before
	 * calling ReadNetSyncInfo().
	 */
	err = DlpReadNetSyncInfo(pconn, &(palm->netsyncinfo));
	switch (err)
	{
	    case DLPSTAT_NOERR:
printf("NetSync info:\n");
printf("\tLAN sync on: %d\n", palm->netsyncinfo.lansync_on);
printf("\thostname: \"%s\"\n", palm->netsyncinfo.synchostname);
printf("\thostaddr: \"%s\"\n", palm->netsyncinfo.synchostaddr);
printf("\tnetmask: \"%s\"\n", palm->netsyncinfo.synchostnetmask);
		break;
	    case DLPSTAT_NOTFOUND:
		printf("No NetSync info.\n");
		break;
	    default:
		fprintf(stderr, "Error reading NetSync info\n");
		return -1;
	}

	/* Get user information from the Palm */
	if ((err = DlpReadUserInfo(pconn, &(palm->userinfo))) < 0)
	{
		fprintf(stderr, "Can't get user info\n");
		return -1;
	}
printf("User info:\n");
printf("\tUserID: 0x%08lx\n", palm->userinfo.userid);
printf("\tViewerID: 0x%08lx\n", palm->userinfo.viewerid);
printf("\tLast sync PC: 0x%08lx\n", palm->userinfo.lastsyncPC);
if (palm->userinfo.lastgoodsync.year == 0)
{
	printf("\tLast good sync: never\n");
} else {
	printf("\tLast good sync: %02d:%02d:%02d %02d/%02d/%02d\n",
	       palm->userinfo.lastgoodsync.hour,
	       palm->userinfo.lastgoodsync.minute,
	       palm->userinfo.lastgoodsync.second,
	       palm->userinfo.lastgoodsync.day,
	       palm->userinfo.lastgoodsync.month,
	       palm->userinfo.lastgoodsync.year);
}
if (palm->userinfo.lastsync.year == 0)
{
	printf("\tLast sync attempt: never\n");
} else {
	printf("\tLast sync attempt: %02d:%02d:%02d %02d/%02d/%02d\n",
	       palm->userinfo.lastsync.hour,
	       palm->userinfo.lastsync.minute,
	       palm->userinfo.lastsync.second,
	       palm->userinfo.lastsync.day,
	       palm->userinfo.lastsync.month,
	       palm->userinfo.lastsync.year);
}
printf("\tUser name: \"%s\"\n", palm->userinfo.username);
printf("\tPassword: <%d bytes>\n", palm->userinfo.passwdlen);

	return 0;
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
