/* coldsync.c
 *
 * $Id: coldsync.c,v 1.4.2.1 1999-07-14 13:39:24 arensb Exp $
 */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>		/* For malloc(), atoi() */
#include <sys/param.h>		/* For MAXPATHLEN */
#include <sys/types.h>		/* For stat() */
#include <sys/stat.h>		/* For stat() */
#include <termios.h>		/* Experimental */
#include <dirent.h>		/* For opendir(), readdir(), closedir() */
#include <string.h>		/* For strrchr() */
#include <unistd.h>		/* For sleep(), getopt() */
#include <ctype.h>		/* For isalpha() and friends */
#include "palm_errno.h"
#include "PConnection.h"
#include "cmp.h"
#include "dlp_cmd.h"
#include "util.h"
#include "coldsync.h"
#include "pdb.h"
#include "conduit.h"

/* XXX - This should be defined elsewhere (e.g., in a config file)
 * (Actually, it should be determined dynamically: try to figure out how
 * fast the serial port can go). The reason there are two macros here is
 * that under Solaris, B19200 != 19200 for whatever reason.
 */
/*  #define SYNC_RATE		57600 */
/*  #define BSYNC_RATE		B57600 */
#define SYNC_RATE		38400
#define BSYNC_RATE		B38400

extern int load_config();
extern int load_palm_config(struct Palm *palm);
/*  int listlocalfiles(struct PConnection *pconn); */
int GetPalmInfo(struct PConnection *pconn, struct Palm *palm);
int UpdateUserInfo(struct PConnection *pconn,
		   const struct Palm *palm, const int success);
int parse_args(int argc, char *argv[]);
void set_debug_level(const char *str);
void usage(int argc, char *argv[]);
void print_version(void);

/* speeds
 * This table provides a list of speeds at which the serial port might be
 * able to communicate.
 * Its structure seems a bit silly, and it is, but only because there are
 * OSes out there that define the B<speed> constants to have values other
 * than their corresponding numeric values.
 */
static struct {
	udword bps;		/* Speed in bits per second, as used by the
				 * CMP layer.
				 */
	speed_t tcspeed;	/* Value to pass to cfset[io]speed() to set
				 * the speed to 'bps'.
				 */
} speeds[] = {
#ifdef B230400
	{ 230400,	B230400 },
#endif	/* B230400 */
#ifdef B115200
	{ 115200,	B115200 },
#endif	/* B115200 */
#ifdef B76800
	{ 76800,	B76800 },
#endif	/* B76800 */
#ifdef B57600
	{ 57600,	B57600 },
#endif	/* B57600 */
#ifdef B38400
	{ 38400,	B38400 },
#endif	/* B38400 */
#ifdef B28800
	{ 28800,	B28800 },
#endif	/* B28800 */
#ifdef B14400
	{ 14400,	B14400 },
#endif	/* B14400 */
#ifdef B9600
	{  9600,	 B9600 },
#endif	/* B9600 */
#ifdef B7200
	{  7200,	 B7200 },
#endif	/* B7200 */
#ifdef B4800
	{  4800,	 B4800 },
#endif	/* B4800 */
#ifdef B2400
	{  2400,	 B2400 },
#endif	/* B2400 */
#ifdef B1200
	{  1200,	 B1200 },
#endif	/* B1200 */
};
#define num_speeds	sizeof(speeds) / sizeof(speeds[0])

struct Palm palm;
int need_slow_sync;
char *synclog = NULL;
int log_size = 0;
int log_len = 0;

struct cmd_opts global_opts;	/* Command-ine options */
struct debug_flags debug;	/* Debugging levels */

int
main(int argc, char *argv[])
{
	struct PConnection *pconn;
	int err;
	int i;

	/* Parse arguments */
	if (parse_args(argc, argv) < 0)
		exit(1);

	MISC_TRACE(2)
	{
		fprintf(stderr, "Options:\n");
		fprintf(stderr, "\tdo_backup: %s\n",
			global_opts.do_backup ? "True" : "False");
		fprintf(stderr, "\tbackupdir: \"%s\"\n",
			global_opts.backupdir == NULL ?
			"(null)" : global_opts.backupdir);
		fprintf(stderr, "\tdo_restore: %s\n",
			global_opts.do_restore ? "True" : "False");
		fprintf(stderr, "\trestoredir: \"%s\"\n",
			global_opts.restoredir == NULL ?
				"(null)" : global_opts.restoredir);
		fprintf(stderr, "\tforce_slow: %s\n",
			global_opts.force_slow ? "True" : "False");
		fprintf(stderr, "\tforce_fast: %s\n",
			global_opts.force_fast ? "True" : "False");
		fprintf(stderr, "\tport: \"%s\"\n",
			global_opts.port == NULL ?
				"(null)" : global_opts.port);
		fprintf(stderr, "\tusername: \"%s\"\n",
			global_opts.username == NULL ?
				"(null)" : global_opts.username);
		fprintf(stderr, "\tusername: %d\n",
			(int) global_opts.uid);
		fprintf(stderr, "\tcheck_ROM: %s\n",
			global_opts.check_ROM ? "True" : "False");
	}

	MISC_TRACE(3)
	{
		fprintf(stderr, "\nDebugging levels:\n");
		fprintf(stderr, "\tSLP:\t%d\n", debug.slp);
		fprintf(stderr, "\tCMP:\t%d\n", debug.cmp);
		fprintf(stderr, "\tPADP:\t%d\n", debug.padp);
		fprintf(stderr, "\tDLP:\t%d\n", debug.dlp);
		fprintf(stderr, "\tDLPC:\t%d\n", debug.dlpc);
		fprintf(stderr, "\tSYNC:\t%d\n", debug.sync);
		fprintf(stderr, "\tMISC:\t%d\n", debug.misc);
	}

	/* Read config files */
	if ((err = load_config()) < 0)
	{
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
	if ((pconn = new_PConnection(global_opts.port)) == NULL)
	{
		fprintf(stderr, "Error: can't open connection.\n");
		/* XXX - Clean up */
		exit(1);
	}

	/* Connect to the Palm */
	if ((err = Connect(pconn, global_opts.port)) < 0)
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

	/* Initialize (per-user) conduits */
	MISC_TRACE(1)
		fprintf(stderr, "Initializing conduits\n");
	if ((err = init_conduits(&palm)) < 0)
	{
		fprintf(stderr, "Can't initialize conduits\n");
		/* XXX - Clean up */
		exit(1);
	}

	if ((err = GetMemInfo(pconn, &palm)) < 0)
	{
		fprintf(stderr, "GetMemInfo() returned %d\n", err);
		/* XXX - Clean up */
		exit(1);
	}

	/* Find out whether we need to do a slow sync or not */
	/* XXX - Actually, it's not as simple as this (see comment below) */
	if (hostid == palm.userinfo.lastsyncPC)
		/* We synced with this same machine last time, so we can do
		 * a fast sync this time.
		 */
		need_slow_sync = 0;
	else
		/* The Palm synced with some other machine, the last time
		 * it synced. We need to do a slow sync.
		 */
		need_slow_sync = 1;

	/* XXX - The desktop needs to keep track of other hosts that it has
	 * synced with, preferably on a per-database basis.
	 * Scenario: I sync with the machine at home, whose hostID is 1.
	 * While I'm driving in to work, the machine at home talks to the
	 * machine at work, whose hostID is 2. I come in to work and sync
	 * with the machine there. The machine there should realize that
	 * even though the Palm thinks it has last synced with machine 1,
	 * everything is up to date on machine 2, so it's okay to do a fast
	 * sync.
	 * This is actually a good candidate for optimization, since it
	 * reduces the amount of time the user has to wait.
	 */

/*  listlocalfiles(pconn); */

	/* Get a list of all databases on the Palm */
	if ((err = ListDBs(pconn, &palm)) < 0)
	{
		fprintf(stderr, "ListDBs returned %d\n", err);
		/* XXX - Clean up */
		exit(1);
	}

	/* Print out the list of databases, for posterity */
	SYNC_TRACE(2)
	{
		fprintf(stderr, "\nDatabase list:\n");
		fprintf(stderr,
			"Name                            flags type crea ver mod. num\n"
			"        ctime                mtime                baktime\n");
		for (i = 0; i < palm.num_dbs; i++)
		{
			fprintf(stderr,
				"%-*s %04x %c%c%c%c %c%c%c%c %3d %08lx\n",
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
			fprintf(stderr, "        "
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
	}

	/* Figure out what to do: backup, restore, or sync */
	if (global_opts.do_backup)
	{
		MISC_TRACE(1)
			fprintf(stderr, "Doing a backup to %s\n",
				global_opts.backupdir);
		err = Backup(pconn, &palm);
		MISC_TRACE(2)
			fprintf(stderr, "Backup() returned %d\n", err);
	} else if (global_opts.do_restore) {
		MISC_TRACE(1)
			fprintf(stderr, "I ought to do a restore from %s\n",
	global_opts.restoredir);
	} else {
		MISC_TRACE(1)
			fprintf(stderr, "Doing a sync.\n");

		/* XXX - Get list of local files: if there are any that
		 * aren't on the Palm, it probably means that they existed
		 * once, but were deleted on the Palm. Assuming that the
		 * user knew what he was doing, these databases should be
		 * deleted. However, just in case, they should be save to
		 * an "attic" directory.
		 */

		/* XXX - If it's configured to install new databases first,
		 * install new databases now.
		 */
#if 0
		err = InstallNewFiles(pconn, &palm);
		if (err < 0)
		{
			fprintf(stderr, "Error installing new files.\n");
			/* XXX - Clean up */
			exit(1);
		}
#endif	/* 0 */

		/* Synchronize the databases */
		for (i = 0; i < palm.num_dbs; i++)
		{
			err = HandleDB(pconn, &palm, i);
			if (err < 0)
			{
				fprintf(stderr, "!!! Oh, my God! A conduit failed! Mayday, mayday! Bailing!\n");
				Disconnect(pconn, DLPCMD_SYNCEND_OTHER);
				/* XXX - Error-handling */
				exit(1);
			}
		}

		/* XXX - If it's configured to install new databases last,
		 * install new databases now.
		 */

		/* XXX - Write updated NetSync info */
		/* Write updated user info */
		if ((err = UpdateUserInfo(pconn, &palm, 1)) < 0)
		{
			fprintf(stderr, "Error writing user info\n");
			/* XXX - Clean up */
			exit(1);
		}

		if (synclog != NULL)
		{
			if ((err = DlpAddSyncLogEntry(pconn, synclog)) < 0)
			{
				fprintf(stderr, "Error writing sync log.\n");
				/* XXX - Clean up */
				exit(1);
			}
		}
	}

	/* Finally, close the connection */
	if ((err = Disconnect(pconn, DLPCMD_SYNCEND_NORMAL)) < 0)
	{
		fprintf(stderr, "Error disconnecting\n");
		/* XXX - Clean up */
		exit(1);
	}

	/* XXX - Clean up conduits */
	if ((err = tini_conduits()) < 0)
	{
		fprintf(stderr, "Error cleaning up conduits\n");
		/* XXX - Clean up */
		exit(1);
	}

	/* XXX - Clean up */
	exit(0);

	/* NOTREACHED */
}

/* Connect
 * Wait for a Palm to show up on the other end.
 */
int
Connect(struct PConnection *pconn,
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

	SYNC_TRACE(5)
		fprintf(stderr, "===== Waiting for wakeup packet\n");
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

	SYNC_TRACE(5)
		fprintf(stderr, "===== Got a wakeup packet\n");

	/* Compose a reply */
	cmpp.type = CMP_TYPE_INIT;
	cmpp.ver_major = 1;	/* XXX - Should be constants in header file */
	cmpp.ver_minor = 1;
	cmpp.rate = SYNC_RATE;
	cmpp.flags = CMP_IFLAG_CHANGERATE;

	SYNC_TRACE(5)
		fprintf(stderr, "===== Sending INIT packet\n");
	cmp_write(pconn, &cmpp);

	SYNC_TRACE(5)
		fprintf(stderr, "===== Finished sending INIT packet\n");

	/* Change the speed */
	/* XXX - This probably goes in Pconn_accept() or something */
	err = tcgetattr(pconn->fd, &term);
	if (err < 0)
	{
		perror("tcgetattr");
		return -1;
	}
	err = cfsetispeed(&term, BSYNC_RATE);
	if (err < 0)
	{
		perror("cfsetispeed");
		return -1;
	}
	err = cfsetospeed(&term, BSYNC_RATE);
	if (err < 0)
	{
		perror("cfsetospeed");
		return -1;
	}
				/* XXX - Instead of syncing at a constant
				 * speed, should figure out the fastest
				 * speed that the serial port will support.
				 */
	err = tcsetattr(pconn->fd, TCSANOW, &term);
	if (err < 0)
	{
		perror("tcsetattr");
		return -1;
	}
	sleep(1);		/* XXX - Why is this necessary? (under
				 * FreeBSD 3.x). Actually, various sensible
				 * things work without the sleep(), but not
				 * with xcopilot (pseudo-ttys).
				 */

	return 0;
}

int
Disconnect(struct PConnection *pconn, const ubyte status)
{
	int err;

	/* Terminate the sync */
	err = DlpEndOfSync(pconn, status);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpEndOfSync: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		return err;
	}
	SYNC_TRACE(5)
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
	if ((err = DlpReadStorageInfo(pconn, CARD0, &last_card, &more,
				      palm->cardinfo)) < 0)
		return -1;

	palm->num_cards = 1;	/* XXX - Hard-wired, for the reasons above */

	MISC_TRACE(4)
	{
		fprintf(stderr, "===== Got memory info:\n");
		fprintf(stderr, "\tTotal size:\t%d\n",
			palm->cardinfo[0].totalsize);
		fprintf(stderr, "\tCard number:\t%d\n",
			palm->cardinfo[0].cardno);
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

		fprintf(stderr, "\tROM databases: %d\n",
			palm->cardinfo[0].rom_dbs);
		fprintf(stderr, "\tRAM databases: %d\n",
			palm->cardinfo[0].ram_dbs);
	}

	/* XXX - Check DLP version */

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
		palm->num_dbs =
			palm->cardinfo[card].ram_dbs;
		if (global_opts.check_ROM)	/* Also considering ROM */
			palm->num_dbs += palm->cardinfo[card].rom_dbs;
		if (palm->num_dbs <= 0)
		{
			/* XXX - Fix this */
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

		/* XXX - If the Palm uses DLP >= 1.2, get multiple
		 * databases at once.
		 */
		iflags = DLPCMD_READDBLFLAG_RAM;
		if (global_opts.check_ROM)
			iflags |= DLPCMD_READDBLFLAG_ROM;
				/* Flags: read at least RAM databases. If
				 * we're even considering the ROM ones,
				 * grab those, too.
				 */

		start = 0;	/* Index at which to start reading */

		/* Collect each database in turn. */
		/* XXX - Should handle older devices that don't return the
		 * number of databases. The easiest thing to do in this
		 * case is probably to resize palm->dblist dynamically
		 * (realloc()) as necessary. In this case, preallocating
		 * its size above becomes just an optimization for the
		 * special case where DLP >= 1.1.
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

	/* XXX - Use 'installdir' from coldsync.h */ 
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
	MISC_TRACE(3)
	{
		printf("System info:\n");
		printf("\tROM version: 0x%08lx\n", palm->sysinfo.rom_version);
		printf("\tLocalization: 0x%08lx\n",
		       palm->sysinfo.localization);
		printf("\tproduct ID: 0x%08lx\n", palm->sysinfo.prodID);
	}

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
		MISC_TRACE(3)
		{
			printf("NetSync info:\n");
			printf("\tLAN sync on: %d\n",
			       palm->netsyncinfo.lansync_on);
			printf("\thostname: \"%s\"\n",
			       palm->netsyncinfo.hostname);
			printf("\thostaddr: \"%s\"\n",
			       palm->netsyncinfo.hostaddr);
			printf("\tnetmask: \"%s\"\n",
			       palm->netsyncinfo.hostnetmask);
		}
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
	MISC_TRACE(3)
	{
		fprintf(stderr, "User info:\n");
		fprintf(stderr, "\tUserID: 0x%08lx\n", palm->userinfo.userid);
		fprintf(stderr, "\tViewerID: 0x%08lx\n",
			palm->userinfo.viewerid);
		fprintf(stderr, "\tLast sync PC: 0x%08lx (%d.%d.%d.%d)\n",
			palm->userinfo.lastsyncPC,
			(int) ((palm->userinfo.lastsyncPC >> 24) & 0xff),
			(int) ((palm->userinfo.lastsyncPC >> 16) & 0xff),
			(int) ((palm->userinfo.lastsyncPC >>  8) & 0xff),
			(int) (palm->userinfo.lastsyncPC & 0xff));
		if (palm->userinfo.lastgoodsync.year == 0)
		{
			fprintf(stderr, "\tLast good sync: never\n");
		} else {
			fprintf(stderr, "\tLast good sync: %02d:%02d:%02d "
				"%02d/%02d/%02d\n",
				palm->userinfo.lastgoodsync.hour,
				palm->userinfo.lastgoodsync.minute,
				palm->userinfo.lastgoodsync.second,
				palm->userinfo.lastgoodsync.day,
				palm->userinfo.lastgoodsync.month,
				palm->userinfo.lastgoodsync.year);
		}
		if (palm->userinfo.lastsync.year == 0)
		{
			fprintf(stderr, "\tLast sync attempt: never\n");
		} else {
			fprintf(stderr,
				"\tLast sync attempt: %02d:%02d:%02d "
				"%02d/%02d/%02d\n",
				palm->userinfo.lastsync.hour,
				palm->userinfo.lastsync.minute,
				palm->userinfo.lastsync.second,
				palm->userinfo.lastsync.day,
				palm->userinfo.lastsync.month,
				palm->userinfo.lastsync.year);
		}
		fprintf(stderr, "\tUser name length: %d\n",
			palm->userinfo.usernamelen);
		fprintf(stderr, "\tUser name: \"%s\"\n",
			palm->userinfo.username == NULL ?
			"(null)" : palm->userinfo.username);
		fprintf(stderr, "\tPassword: <%d bytes>\n",
			palm->userinfo.passwdlen);
		MISC_TRACE(6)
			debug_dump(stderr, "PASS", palm->userinfo.passwd,
				   palm->userinfo.passwdlen);
	}

	return 0;
}

/* UpdateUserInfo
 * Update the Palm's user info. 'success' indicates whether the sync was
 * successful.
 */
int
UpdateUserInfo(struct PConnection *pconn,
	       const struct Palm *palm,
	       const int success)
{
	int err;
	struct dlp_setuserinfo userinfo;
			/* Fill this in with new values */
	
	userinfo.modflags = 0;		/* Initialize modification flags */
	userinfo.usernamelen = 0;

	MISC_TRACE(1)
		fprintf(stderr, "* UpdateUserInfo:\n");

	/* Does this Palm have a user ID yet? */
	/* XXX - If the Palm has a user ID, but it's not that of the
	 * current user, it should be possible to overwrite it. Perhaps
	 * this should be an "administrative mode" option.
	 */
	if (palm->userinfo.userid == 0)
	{
		MISC_TRACE(3)
			fprintf(stderr, "Setting UID to %d (0x%04x)\n",
				(int) user_uid,
				(unsigned int) user_uid);
		/* XXX - Fill this in */
		userinfo.userid = (udword) user_uid;
		userinfo.modflags |= DLPCMD_MODUIFLAG_USERID;
					/* Set modification flag */
	}

	/* Fill in this machine's host ID as the last sync PC */
	MISC_TRACE(3)
		fprintf(stderr, "Setting lastsyncPC to 0x%08lx\n", hostid);
	userinfo.lastsyncPC = hostid;
	userinfo.modflags |= DLPCMD_MODUIFLAG_SYNCPC;

	/* If successful, update the "last successful sync" date */
	if (success)
	{
		time_t now;		/* Current time */

		MISC_TRACE(3)
			fprintf(stderr, "Setting last sync time to now\n");
		time(&now);		/* Get current time */
		time_time_t2dlp(now, &userinfo.lastsync);
					/* Convert to DLP time */
		userinfo.modflags |= DLPCMD_MODUIFLAG_SYNCDATE;
	}

	/* Fill in the user name if there isn't one, or if it has changed
	 */
	if ((palm->userinfo.usernamelen == 0) ||
	    (strcmp(palm->userinfo.username, user_fullname) != 0))
	{
		MISC_TRACE(3)
			fprintf(stderr, "Setting user name to \"%s\"\n",
				user_fullname);
		userinfo.username = user_fullname;
		userinfo.usernamelen = strlen(userinfo.username)+1;
		userinfo.modflags |= DLPCMD_MODUIFLAG_USERNAME;
		MISC_TRACE(3)
			fprintf(stderr, "User name length == %d\n",
				userinfo.usernamelen);
	}

	/* Send the updated user info to the Palm */
	err = DlpWriteUserInfo(pconn,
			       &userinfo);
	if (err != DLPSTAT_NOERR)
	{
		fprintf(stderr, "DlpWriteUserInfo failed: %d\n", err);
		return -1;
	}

	return 0;		/* Success */
}

/* find_max_speed
 * Find the maximum speed at which the serial port (pconn->fd) is able to
 * talk. Returns an index into the 'speeds' array, or -1 in case of error.
 * XXX - Okay, now where does this get called? The logical place is right
 * after the open() in new_PConnection(). But the result from this function
 * is used in this file, when setting the speed for the main part of the
 * sync.
 */
int
find_max_speed(struct PConnection *pconn)
{
	int i;
	struct termios term;

	tcgetattr(pconn->fd, &term);
	/* XXX - Error-checking */

	/* Step through the array of speeds, one at a time. Stop as soon as
	 * we find a speed that works, since the 'speeds' array is sorted
	 * in order of decreasing desirability (i.e., decreasing speed).
	 */
	for (i = 0; i < num_speeds; i++)
	{
/*  fprintf(stderr, "Trying %ld bps\n", speeds[i].bps); */
		if (cfsetispeed(&term, speeds[i].tcspeed) == 0 &&
		    cfsetospeed(&term, speeds[i].tcspeed) == 0 &&
		    tcsetattr(pconn->fd, TCSANOW, &term) == 0)
			return i;
/*  fprintf(stderr, "Nope\n"); */
	}

/*  fprintf(stderr, "Couldn't find a suitable speed.\n"); */
	return -1;
}

/* parse_args
 * Parse command-line arguments, and fill in the appropriate slots in
 * 'global_opts'.
 */
/* XXX - Command-line options to add or implement:
 * -u <user>:	run as <user>
 * -b <dir>:	perform a full backup to <dir>
 * -r <dir>:	perform a full restore from <dir>
 * -c:		With -b: remove any files in backup dir that aren't on Palm.
 *		With -r: remove any files on Palm that aren't in backup dir.
 * -i <file>:	upload (install) <file>
 * -D <level>:	set debugging to <level>. Probably want to do something
 *		like sendmail, and allow user to specify debugging level
 *		for various facilities.
 * -p:		print PID to stdout, like 'amd'. Or maybe just do this by
 *		default when running in daemon mode.
 * -d <dir>:	Sync with <dir> rather than ~/.palm
 */
int
parse_args(int argc, char *argv[])
{
#if 0
	extern char *optarg;	/* getopt() option argument */
	extern int optind;	/* getopt() option index into argv[] */
#endif	/* 0 */
	int oldoptind;		/* Previous value of 'optind', to allow us
				 * to figure out exactly which argument was
				 * bogus, and thereby print descriptive
				 * error messages.
				 */
	int arg;		/* Current option */

	opterr = 0;		/* Don't print getopt() error messages. */

	/* Initialize the options to sane values */
	global_opts.config_file = NULL;	/* XXX - Should be some constant */
	global_opts.do_backup = False;
	global_opts.backupdir = NULL;
	global_opts.do_restore = False;
	global_opts.restoredir = NULL;
	global_opts.force_slow = False;
	global_opts.force_fast = False;
	global_opts.port = NULL;
	global_opts.username = NULL;
	global_opts.uid = (uid_t) -1;	/* Run as root by default */
	global_opts.check_ROM = False;

	/* Initialize the debugging levels to 0 */
	debug.slp	= 0;
	debug.cmp	= 0;
	debug.padp	= 0;
	debug.dlp	= 0;
	debug.dlpc	= 0;
	debug.sync	= 0;
	debug.misc	= 0;

	oldoptind = optind;		/* Initialize "last argument"
					 * index.
					 */

	/* Get each option in turn */
	while ((arg = getopt(argc, argv, ":hVSFRu:b:r:p:f:d:")) != -1)
	{
		switch (arg)
		{
		    case 'h':	/* -h: Print usage message and exit */
			usage(argc,argv);
			return -1;

		    case 'V':	/* -V: Print version number and exit */
			print_version();
			return -1;

		    case 'u':	/* -u <user|uid>: Run as user <user>, or
				 * uid <uid>.
				 */
			printf("User \"%s\"\n", optarg);
			if (isalpha(optarg[0]))
				/* User specified as username */
				global_opts.username = optarg;
			else
				/* User specified as UID */
				global_opts.uid = (uid_t) atoi(optarg);
			break;

		    case 'b':	/* -b <dir>: Do a full backup to <dir> */
			global_opts.do_backup = True;
			global_opts.backupdir = optarg;
			break;

		    case 'r':	/* -r <dir>: Do a restore from <dir> */
			global_opts.do_restore = True;
			global_opts.restoredir = optarg;
			break;

		    case 'p':	/* -p <device>: Listen on serial port
				 * <device>
				 */
			global_opts.port = optarg;
			break;

		    case 'f':	/* -f <file>: Read configuration from
				 * <file>.
				 */
			/* XXX - Implement config files */
			global_opts.config_file = optarg;
			break;

		    case 'S':	/* -S: Force slow sync */
			global_opts.force_slow = True;
			break;

		    case 'F':	/* -F: Force fast sync */
			global_opts.force_fast = True;
			break;

		    case 'R':	/* -R: Consider ROM databases */
			global_opts.check_ROM = True;
			break;

		    case 'd':	/* -d <level>: Debugging level */
			set_debug_level(optarg);
			break;

		    case '?':	/* Unknown option */
			fprintf(stderr, "Unrecognized option: \"%s\"\n",
				argv[oldoptind]);
			usage(argc, argv);
			return -1;

		    case ':':	/* An argument required an option, but none
				 * was given (e.g., "-u" instead of "-u
				 * daemon").
				 */
			fprintf(stderr, "Missing option argument after \"%s\"\n",
				argv[oldoptind]);
			usage(argc, argv);
			return -1;

		    default:
			fprintf(stderr,
				"You specified an apparently legal option (\"-%c\"), but I don't know what\n"
				"to do with it. This is a bug. Please notify the maintainer.\n", arg);
			return -1;
			break;
		}
		oldoptind = optind;	/* Remember the current "next
					 * argument", in case it causes an
					 * error.
					 */
	}

	/* XXX - Check for trailing arguments. What to do? Barf? */

	/* XXX - Sanity checks:
	 * - Can't specify username and UID.
	 */
	/* Sanity checks */

	/* Can't back up and restore at the same time */
	if (global_opts.do_backup &&
	    global_opts.do_restore)
	{
		fprintf(stderr, "Error: Can't specify backup and restore at the same time.\n");
		usage(argc, argv);
		return -1;
	}

	/* Can't force both a slow and a fast sync */
	if (global_opts.force_slow &&
	    global_opts.force_fast)
	{
		fprintf(stderr, "Error: Can't force slow and fast sync at the same time.\n");
		usage(argc, argv);
		return -1;
	}

	/* Can't specify both a username and a UID. */
	if ((global_opts.username != NULL) &&
	    (global_opts.uid != -1))
	{
		fprintf(stderr, "Error: Can't specify both a user name and a UID.\n");
		usage(argc, argv);
		return -1;
	}
	/* XXX - If username specified, ought to make sure that user
	 * exists. Or should this be deferred until later?
	 */

	/* Make sure at least one port was specified */
	/* XXX - This really ought to be specified in the config file(s) */
	if (global_opts.port == NULL)
	{
		fprintf(stderr, "Error: no port specified.\n");
		usage(argc, argv);
		return -1;
	}

	return 0;
}

/* set_debug_level
 * Set a debugging/trace value. These are settable on a per-facility basis
 * (see struct debug_flags in "coldsync.h"). Thus, specifying
 * "-d SLP:1 -d PADP:10" will give the barest minimum of information as to
 * what is happening in the SLP layer, but will spew gobs of information
 * about the PADP layer, including dumps of every packet going back and
 * forth.
 * Note to hackers: as a general rule, a debugging level of 0 means no
 * debugging messages (this is the default). 1 means give the user some
 * idea of what is going on. 5 means print a message for every packet; 6
 * means print the contents of every packet. 10 means... well, let's just
 * not think about that, shall we?
 * These numbers are just guidelines, though. Your circumstances will vary.
 *
 * 'str' can take on one of the following
 * forms:
 *	FAC	Set facility FAC to level 1
 *	FAC:N	Set facility FAC to level N
 */
void
set_debug_level(const char *str)
{
	const char *lvlstr;	/* Pointer to trace level in 'str' */
	int lvl;		/* Trace level */

	/* See if 'str' contains a colon */
	if ((lvlstr = strchr(str, ':')) != NULL)
	{
		/* 'str' contains a colon. Parse the string */
		lvlstr++;		/* Make 'lvlstr' point to the level
					 * number */
		lvl = atoi(lvlstr);
	} else {
		/* 'str' does not contain a colon. Set the trace level to 1 */
		lvl = 1;
	}

	/* Set the appropriate debugging facility. */
	if (strncasecmp(str, "slp:", 4) == 0)
		debug.slp = lvl;
	else if (strncasecmp(str, "cmp:", 4) == 0)
		debug.cmp = lvl;
	else if (strncasecmp(str, "padp:", 5) == 0)
		debug.padp = lvl;
	else if (strncasecmp(str, "dlpc:", 5) == 0)
		debug.dlpc = lvl;
	else if (strncasecmp(str, "dlp:", 4) == 0)
		debug.dlp = lvl;
	else if (strncasecmp(str, "sync:", 5) == 0)
		debug.sync = lvl;
	else if (strncasecmp(str, "misc:", 5) == 0)
		debug.misc = lvl;
	else {
		fprintf(stderr, "Unknown facility \"%s\"\n", str);
	}
}

/* usage
 * Print out a usage string.
 */
void
usage(int argc, char *argv[])
{
	printf("Usage: %s [options] -p port\n"
	       "Options:\n"
	       "\t-h:\t\tPrint this help message and exit.\n"
	       "\t-V:\t\tPrint version and exit.\n"
/*  	       "\t-u <user|uid>:\tRun under the given UID.\n" */
	       "\t-b <dir>:\tPerform a backup to <dir>.\n"
	       "\t-r <dir>:\tRestore from <dir>.\n"
	       "\t-S:\t\tForce slow sync.\n"
	       "\t-F:\t\tForce fast sync.\n"
	       "\t-R:\t\tCheck ROM databases.\n"
	       "\t-p <port>:\tListen on device <port>\n"
	       "\t-d <fac[:level]>:\tSet debugging level.\n"
	       ,
	       argv[0]);
}

/* print_version
 * Print out the version of ColdSync.
 */
void
print_version(void)
{
	printf("%s version %s\n",
	       /* These two strings are defined in "config.h" */
	       PACKAGE,
	       VERSION);
	/* XXX - Ought to print out other information, e.g., copyright,
	 * compile-time flags, optional packages, maybe OS name and
	 * version, who compiled it and when, etc.
	 */
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
