/* coldsync.c
 *
 * $Id: coldsync.c,v 1.1 1999-07-04 13:40:32 arensb Exp $
 */
#include <stdio.h>
#include <stdlib.h>		/* For malloc(), atoi() */
#include <sys/param.h>		/* For MAXPATHLEN */
#include <sys/types.h>		/* For stat() */
#include <sys/stat.h>		/* For stat() */
#include <termios.h>		/* Experimental */
#include <dirent.h>		/* For opendir(), readdir(), closedir() */
#include <string.h>		/* For strrchr() */
#include <unistd.h>		/* For sleep() */
#include <ctype.h>		/* For isalpha() and friends */
#include "config.h"
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
#define SYNC_RATE		57600
#define BSYNC_RATE		B57600

extern int slp_debug;
extern int cmp_debug;
extern int dlp_debug;
extern int dlpc_debug;
extern int sync_debug;

extern int load_config();
extern int load_palm_config(struct Palm *palm);
/*  int listlocalfiles(struct PConnection *pconn); */
int GetPalmInfo(struct PConnection *pconn, struct Palm *palm);
int UpdateUserInfo(struct PConnection *pconn,
		   const struct Palm *palm, const int success);
int parse_args(int argc, char *argv[]);
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
char *log = NULL;
int log_size = 0;
int log_len = 0;

struct cmd_opts global_opts;	/* Command-ine options */

/* XXX - Command-line options:
 * -u <user>:	run as <user>
 * -b <dir>:	perform a full backup to <dir>
 * -r <dir>:	perform a full restore from <dir>
 * -c:		With -b: remove any files in backup dir that aren't on Palm.
 *		With -r: remove any files on Palm that aren't in backup dir.
 * -s:		force slow sync
 * -i <file>:	upload (install) <file>
 * -D <level>:	set debugging to <level>. Probably want to do something
 *		like sendmail, and allow user to specify debugging level
 *		for various facilities.
 * -h:		print help message.
 * -p:		print PID to stdout, like 'amd'. Or maybe just do this by
 *		default when running in daemon mode.
 */

int
main(int argc, char *argv[])
{
	struct PConnection *pconn;
	int err;
	int i;

	/* Parse arguments */
	if (parse_args(argc, argv) < 0)
		exit(1);

fprintf(stderr, "Options:\n");
fprintf(stderr, "do_backup: %s\n",
	global_opts.do_backup ? "True" : "False");
fprintf(stderr, "backupdir: \"%s\"\n",
	global_opts.backupdir);
fprintf(stderr, "do_restore: %s\n",
	global_opts.do_restore ? "True" : "False");
fprintf(stderr, "restoredir: \"%s\"\n",
	global_opts.restoredir);
fprintf(stderr, "do_clean: %s\n",
	global_opts.do_clean ? "True" : "False");
fprintf(stderr, "force_slow: %s\n",
	global_opts.force_slow ? "True" : "False");
fprintf(stderr, "force_fast: %s\n",
	global_opts.force_fast ? "True" : "False");
fprintf(stderr, "port: \"%s\"\n",
	global_opts.port);
fprintf(stderr, "username: \"%s\"\n",
	global_opts.username);
fprintf(stderr, "username: %d\n",
	global_opts.uid);

	/* Read config files */
	if ((err = load_config()) < 0)
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

	/* XXX - It should be possible to force a slow sync */
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

	/* XXX - Get list of local files: if there are any that aren't on
	 * the Palm, it probably means that they existed once, but were
	 * deleted on the Palm. Assuming that the user knew what he was
	 * doing, these databases should be deleted. However, just in case,
	 * they should be save to an "attic" directory.
	 */

	/* XXX - Figure out which conduits exist for each of the existing
	 * databases; if there are multiple conduits for a given database,
	 * figure out which one to use; if it can be "primed", do so.
	 */

	/* XXX - If it's configured to install new databases first, install
	 * new databases now.
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
sync_debug = 10;
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

	/* XXX - If it's configured to install new databases last, install
	 * new databases now.
	 */
/*  slp_debug = cmp_debug = dlp_debug = dlpc_debug = sync_debug = 100; */

	/* XXX - Write updated NetSync info */
	/* Write updated user info */
	if ((err = UpdateUserInfo(pconn, &palm, 1)) < 0)
	{
		fprintf(stderr, "Error writing user info\n");
		/* XXX - Clean up */
		exit(1);
	}

	if (log != NULL)
	{
		if ((err = DlpAddSyncLogEntry(pconn, log)) < 0)
		{
			fprintf(stderr, "Error writing sync log.\n");
			/* XXX - Clean up */
			exit(1);
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

/* XXX - Make this conditional upon sync_debug */

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

/* XXX - Check DLP version */
/*  fprintf(stderr, "\tROM databases: %d\n", palm->cardinfo[0].rom_dbs); */
/*  fprintf(stderr, "\tRAM databases: %d\n", palm->cardinfo[0].ram_dbs); */

	return 0;
}

/* XXX - This ought to be a run-time option */
#define CHECK_ROM_DBS	0

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
#if CHECK_ROM_DBS
			palm->cardinfo[card].rom_dbs +
#endif
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

		/* XXX - If the Palm uses DLP >= 1.2, get multiple
		 * databases at once.
		 */
		iflags = DLPCMD_READDBLFLAG_RAM
#if CHECK_ROM_DBS
			| DLPCMD_READDBLFLAG_ROM
#endif
			;
				/* Flags: read both ROM and RAM databases */

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
dlpc_debug = 5;
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
printf("\thostname: \"%s\"\n", palm->netsyncinfo.hostname);
printf("\thostaddr: \"%s\"\n", palm->netsyncinfo.hostaddr);
printf("\tnetmask: \"%s\"\n", palm->netsyncinfo.hostnetmask);
		break;
	    case DLPSTAT_NOTFOUND:
		printf("No NetSync info.\n");
		break;
	    default:
		fprintf(stderr, "Error reading NetSync info\n");
		return -1;
	}

	/* Get user information from the Palm */
/*  dlpc_debug = 10; */
/*  dlp_debug = 10; */
	if ((err = DlpReadUserInfo(pconn, &(palm->userinfo))) < 0)
	{
		fprintf(stderr, "Can't get user info\n");
		return -1;
	}
/*  dlp_debug = 1; */
/*  dlpc_debug = 1; */
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
printf("\tUser name length: %d\n", palm->userinfo.usernamelen);
printf("\tUser name: \"%s\"\n", palm->userinfo.username);
printf("\tPassword: <%d bytes>\n", palm->userinfo.passwdlen);
debug_dump(stdout, "PASS", palm->userinfo.passwd, palm->userinfo.passwdlen);

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

fprintf(stderr, "* UpdateUserInfo:\n");
	/* Does this Palm have a user ID yet? */
	/* XXX - If the Palm has a user ID, but it's not that of the
	 * current user, it should be possible to overwrite it. Perhaps
	 * this should be an "administrative mode" option.
	 */
	if (palm->userinfo.userid == 0)
	{
fprintf(stderr, "Setting UID to %d (0x%04x)\n", user_uid, user_uid);
		/* XXX - Fill this in */
		userinfo.userid = (udword) user_uid;
		userinfo.modflags |= DLPCMD_MODUIFLAG_USERID;
					/* Set modification flag */
	}

	/* Fill in this machine's host ID as the last sync PC */
fprintf(stderr, "Setting lastsyncPC to 0x%08lx\n", hostid);
	userinfo.lastsyncPC = hostid;
	userinfo.modflags |= DLPCMD_MODUIFLAG_SYNCPC;

	/* If successful, update the "last successful sync" date */
	if (success)
	{
		time_t now;		/* Current time */

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
fprintf(stderr, "Setting user name to \"%s\"\n", user_fullname);
		userinfo.username = user_fullname;
		userinfo.usernamelen = strlen(userinfo.username)+1;
		userinfo.modflags |= DLPCMD_MODUIFLAG_USERNAME;
fprintf(stderr, "User name length == %d\n", userinfo.usernamelen);
	}

	/* Send the updated user info to the Palm */
/*  dlpc_debug = 10; */
/*  dlp_debug = 10; */
/*  cmp_debug = 10; */
/*  slp_debug = 10; */
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
int
parse_args(int argc, char *argv[])
{
	int arg;	/* Index of current argument */
	char *nextarg;	/* Pointer to next argument: options can be
			 * specified either as "-udaemon" or "-u daemon";
			 * 'nextarg' points to the beginning of "daemon".
			 */

	/* Initialize the options to sane values */
	global_opts.do_backup = False;
	global_opts.backupdir = NULL;
	global_opts.do_restore = False;
	global_opts.restoredir = NULL;
	global_opts.do_clean = False;
	global_opts.force_slow = False;
	global_opts.force_fast = False;
	global_opts.port = NULL;
	global_opts.username = NULL;
	global_opts.uid = (uid_t) 0;

	/* Iterate over each command-line argument */
	for (arg = 1; arg < argc; arg++)
	{
		if (argv[arg][0] != '-')
		{
			/* This argument doesn't begin with a dash. It must
			 * be the serial port device.
			 */
			if (arg != argc-1)
			{
				/* Bad arguments */
				usage(argc, argv);
				return -1;
			}

			/* Got a serial device name */
			global_opts.port = argv[arg];
			continue;
		}

		switch (argv[arg][1])
		{
		    case 'u':	/* -u <username|uid>: specify user as which
				 * coldsync is to run.
				 */
			if (argv[arg][2] == '\0')
			{
				/* Option given as "-u foo" */
				if (arg >= argc-1)
				{
					/* Oops! No next argument */
					usage(argc, argv);
					return -1;
				}
				nextarg = argv[arg+1];

				arg++;	/* Skip this argument in the loop */
			} else
				/* Option given as "-ufoo" */
				nextarg = argv[arg]+2;

			if (isalpha(nextarg[0]))
				/* User name */
				global_opts.username = nextarg;
			else if (isdigit(nextarg[0]) ||
				 nextarg[0] == '-')
				/* UID */
				global_opts.uid =
					(uid_t) atoi(nextarg);
			break;

		    case 'b':	/* -b <dir>: do a full backup to <dir> */
			if (argv[arg][2] == '\0')
			{
				/* Option given as "-b foo" */
				if (arg >= argc-1)
				{
					/* Oops! No next argument */
					usage(argc, argv);
					return -1;
				}
				nextarg = argv[arg+1];

				arg++;	/* Skip this argument in the loop */
			} else
				/* Option given as "-bfoo" */
				nextarg = argv[arg]+2;

			global_opts.do_backup = True;
			global_opts.backupdir = nextarg;

			break;

		    case 'r':	/* -r <dir>: do a restore from <dir> */
			if (argv[arg][2] == '\0')
			{
				/* Option given as "-u foo" */
				if (arg >= argc-1)
				{
					/* Oops! No next argument */
					usage(argc, argv);
					return -1;
				}
				nextarg = argv[arg+1];

				arg++;	/* Skip this argument in the loop */
			} else
				/* Option given as "-ufoo" */
				nextarg = argv[arg]+2;

			global_opts.do_restore = True;
			global_opts.restoredir = nextarg;

			break;

		    case 'c':	/* -c: Clean: remove all files/databases
				 * before doing backup or restore.
				 */
			global_opts.do_clean = True;
			break;

		    case 's':	/* -s: Force slow sync */
			global_opts.force_slow = True;
			break;

		    case 'f':	/* -f: Force fast sync */
			global_opts.force_fast = True;
			break;

		    case 'h':	/* -h: Print help message and exit */
			usage(argc, argv);
			return -1;	/* Probably bogus, but what the hell */

		    case 'v':	/* -v: Print version and exit */
			print_version();
			return -1;	/* Probably bogus, but what the hell */

		    default:	/* Unknown option */
			fprintf(stderr, "Error: unknown option: %s\n",
				argv[arg]);
			usage(argc,argv);
			return -1;
		}
	}

	/* General sanity checking */

	if (global_opts.force_slow &&
	    global_opts.force_fast)
	{
		fprintf(stderr, "Error: can't force slow and fast sync at the same time.\n");
		usage(argc, argv);
		return -1;
	}

	if (global_opts.port == NULL)
	{
		fprintf(stderr, "Error: no port specified.\n");
		usage(argc, argv);
		return -1;
	}

	return 0;		/* Success */
}

/* usage
 * Print out a usage string.
 */
void
usage(int argc, char *argv[])
{
	printf("Usage: %s [options] port\n"
	       "Options:\n"
	       "\t-u <user|uid>:\tRun under the given UID.\n"
	       "\t-b <dir>:\tPerform a backup to <dir>.\n"
	       "\t-r <dir>:\tRestore from <dir>.\n"
	       "\t-c:\t\tClean: with -b or -r, remove extraneous files.\n"
	       "\t-s:\t\tForce slow sync.\n"
	       "\t-f:\t\tForce fast sync.\n"
	       "\t-h:\t\tPrint this help message and exit.\n"
	       "\t-v:\t\tPrint version and exit.\n"
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
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
