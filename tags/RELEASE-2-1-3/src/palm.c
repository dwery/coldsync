/* palm.c
 *
 * Functions pertaining to 'struct Palm's. See description in "palm.h"
 *
 *	Copyright (C) 2000-2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: palm.c,v 2.17.2.1 2001-06-30 07:09:13 arensb Exp $
 */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>		/* For malloc(), free() */
#include <string.h>		/* For bzero() */
#include "coldsync.h"

/* Include I18N-related stuff, if necessary */
#if HAVE_LIBINTL_H
#  include <locale.h>		/* For setlocale() and friends */
#  include <libintl.h>
#endif	/* HAVE_LIBINTL_H */

#include "palm.h"
#include "cs_error.h"
#include "pconn/palm_types.h"

/* Private helper functions: the fetch_*() functions do the actual work of
 * fetching data from the Palm. Other functions give access to specific
 * bits that the caller cares about.
 */
static int fetch_meminfo(struct Palm *palm);
static int fetch_sysinfo(struct Palm *palm);
static int fetch_netsyncinfo(struct Palm *palm);
static int fetch_userinfo(struct Palm *palm);
static int fetch_serial(struct Palm *palm);

/* special_snums
 * This exists mainly to accomodate the Handspring Visor: although it has a
 * 'snum' ROM token, and it's possible to read that memory location, that
 * location in memory contains a string of 0xff characters.
 * It might be tempting to pretend that the Visor doesn't have a serial
 * number. However, this string appears to uniquely differentiate Visors
 * from other PalmOS devices (e.g., the PalmPilot Pro, which doesn't even
 * have a 'snum' ROM token).
 *
 * The 'special_snums' array maps the binary string found for the serial
 * number, to a printable alias.
 */
static struct {
	char *real;		/* Real serial number value */
	int real_len;		/* Length of 'real' */
	char *alias;		/* String to return instead of real serial
				 * number. */
} special_snums[] = {
	{ "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff", 12, "*Visor*" },
};
const int num_special_snums = sizeof(special_snums) / sizeof(special_snums[0]);

/* new_Palm
 * Constructor. Allocates a 'struct Palm' and fills it in with special
 * values. These values all mean, "I don't know yet". E.g., 'num_dbs' is
 * set to -1. Later, accessor functions will use these special values to
 * find out whether they need to query the Palm or use a cached value.
 */
struct Palm *
new_Palm(PConnection *pconn)
{
	struct Palm *retval;

	/* Allocate memory */
	if ((retval = (struct Palm *) malloc(sizeof(struct Palm))) == NULL)
		return NULL;		/* Out of memory */

	/* Fill in the fields. All of these are initialized to special
	 * values meaning "I haven't found out yet". The real values will
	 * be filled in later.
	 */
	retval->pconn_ = pconn;

	retval->have_sysinfo_ = False;
	retval->have_userinfo_ = False;
	retval->have_netsyncinfo_ = False;
	bzero((void *) &(retval->serial_), SNUM_MAX);
	retval->serial_len_ = -1;
	retval->num_cards_ = -1;
	retval->cardinfo_ = NULL;
	retval->have_all_DBs_ = False;
	retval->num_dbs_ = -1;
	retval->dblist_ = NULL;
	retval->dbit_ = 0;

	return retval;
}

/* free_Palm
 * Destructor. Free a 'struct Palm'. The PConnection that was passed to it
 * remains untouched; that's the caller's responsibility.
 */
void
free_Palm(struct Palm *palm)
{
	if (palm->cardinfo_ != NULL)
		/* 'cardinfo' is an array. It gets freed all at once */
		free(palm->cardinfo_);

	if (palm->dblist_ != NULL)
		/* 'dblist' is an array. It gets freed all at once */
		free(palm->dblist_);

	free(palm);
}

/* fetch_meminfo
 * Get info about the memory on the Palm and record it in 'palm'.
 */
static int
fetch_meminfo(struct Palm *palm)
{
	int err;
	ubyte last_card;
	ubyte more;

	/* Allocate space for the card info */
	if ((palm->cardinfo_ = (struct dlp_cardinfo *)
	     malloc(sizeof(struct dlp_cardinfo))) == NULL)
		return -1;

	/* Ask the Palm about each memory card in turn */
	/* XXX - Actually, it doesn't: it just asks about memory card 0;
	 * the 'more' return value should be non-zero if there are more
	 * cards to be read, but it's always one on every Palm I've tried
	 * this on.
	 */
	if ((err = DlpReadStorageInfo(palm->pconn_, CARD0, &last_card, &more,
				      palm->cardinfo_)) < 0)
	{
		switch (palm_errno)
		{
		    case PALMERR_TIMEOUT:
			cs_errno = CSE_NOCONN;
			break;
		    default:
			break;
		}

		return -1;
	}

	palm->num_cards_ = 1;	/* XXX - Hard-wired, for the reasons above */
			/* This also indicates to other functions that
			 * fetch_meminfo() has already been called.
			 */

	MISC_TRACE(4)
	{
		fprintf(stderr, "===== Got memory info:\n");
		fprintf(stderr, "\tTotal size:\t%d\n",
			palm->cardinfo_[CARD0].totalsize);
		fprintf(stderr, "\tCard number:\t%d\n",
			palm->cardinfo_[CARD0].cardno);
		fprintf(stderr, "\tCard version: %d (0x%02x)\n",
			palm->cardinfo_[CARD0].cardversion,
			palm->cardinfo_[CARD0].cardversion);
		fprintf(stderr, "\tCreation time: %02d:%02d:%02d %d/%d/%d\n",
			palm->cardinfo_[CARD0].ctime.second,
			palm->cardinfo_[CARD0].ctime.minute,
			palm->cardinfo_[CARD0].ctime.hour,
			palm->cardinfo_[CARD0].ctime.day,
			palm->cardinfo_[CARD0].ctime.month,
			palm->cardinfo_[CARD0].ctime.year);
		fprintf(stderr, "\tROM: %ld (0x%04lx)\n",
			palm->cardinfo_[CARD0].rom_size,
			palm->cardinfo_[CARD0].rom_size);
		fprintf(stderr, "\tRAM: %ld (0x%04lx)\n",
			palm->cardinfo_[CARD0].ram_size,
			palm->cardinfo_[CARD0].ram_size);
		fprintf(stderr, "\tFree RAM: %ld (0x%04lx)\n",
			palm->cardinfo_[CARD0].free_ram,
			palm->cardinfo_[CARD0].free_ram);
		fprintf(stderr, "\tCard name (%d) \"%s\"\n",
			palm->cardinfo_[CARD0].cardname_size,
			palm->cardinfo_[CARD0].cardname);
		fprintf(stderr, "\tManufacturer name (%d) \"%s\"\n",
			palm->cardinfo_[CARD0].manufname_size,
			palm->cardinfo_[CARD0].manufname);

		fprintf(stderr, "\tROM databases: %d\n",
			palm->cardinfo_[CARD0].rom_dbs);
		fprintf(stderr, "\tRAM databases: %d\n",
			palm->cardinfo_[CARD0].ram_dbs);
	}

	return 0;
}

/* fetch_sysinfo
 * Read system info from Palm.
 */
static int
fetch_sysinfo(struct Palm *palm)
{
	int err;

	MISC_TRACE(3)
		fprintf(stderr, "Fetching SysInfo\n");

	/* Get system information about the Palm */
	if ((err = DlpReadSysInfo(palm->pconn_, &(palm->sysinfo_))) < 0)
	{
		switch (palm_errno)
		{
		    case PALMERR_TIMEOUT:
			cs_errno = CSE_NOCONN;
			break;
		    default:
			break;
		}

		/* XXX - This message doesn't really belong here */
		Error(_("Can't get system info."));
		return -1;
	}
	MISC_TRACE(3)
	{
		fprintf(stderr, "System info:\n");
		fprintf(stderr, "\tROM version: 0x%08lx\n",
			palm->sysinfo_.rom_version);
		fprintf(stderr, "\tLocalization: 0x%08lx\n",
			palm->sysinfo_.localization);
		fprintf(stderr, "\tproduct ID: 0x%08lx\n",
			palm->sysinfo_.prodID);
		fprintf(stderr, "\tDLP version: %d.%d\n",
			palm->sysinfo_.dlp_ver_maj,
			palm->sysinfo_.dlp_ver_min);
		fprintf(stderr, "\tProduct compatibility version: %d.%d\n",
			palm->sysinfo_.comp_ver_maj,
			palm->sysinfo_.comp_ver_min);
		fprintf(stderr, "\tMax. record size: %ld (0x%08lx)\n",
			palm->sysinfo_.max_rec_size,
			palm->sysinfo_.max_rec_size);
	}

	/* Note that we've fetched this information */
	palm->have_sysinfo_ = True;

	return 0;		/* Success */
}

/* fetch_netsyncinfo
 * Fetch NetSync information from the Palm.
 */
static int
fetch_netsyncinfo(struct Palm *palm)
{
	int err;

	MISC_TRACE(3)
		fprintf(stderr, "Fetching NetSyncInfo\n");

	/* Get NetSync information from the Palm */
	/* XXX - Need to check some stuff, first: if it's a pre-1.1 Palm,
	 * it doesn't know about NetSync. HotSync tries to
	 * OpenDB("NetSync"), then it tries ReadFeature('netl',0), before
	 * calling ReadNetSyncInfo().
	 */
	err = DlpReadNetSyncInfo(palm->pconn_, &(palm->netsyncinfo_));
	switch (err)
	{
	    case DLPSTAT_NOERR:
		MISC_TRACE(3)
		{
			fprintf(stderr, "NetSync info:\n");
			fprintf(stderr, "\tLAN sync on: %d\n",
				palm->netsyncinfo_.lansync_on);
			fprintf(stderr, "\thostname: \"%s\"\n",
				palm->netsyncinfo_.hostname);
			fprintf(stderr, "\thostaddr: \"%s\"\n",
				palm->netsyncinfo_.hostaddr);
			fprintf(stderr, "\tnetmask: \"%s\"\n",
				palm->netsyncinfo_.hostnetmask);
		}
		break;
	    case DLPSTAT_NOTFOUND:
		printf(_("No NetSync info.\n"));
		break;
	    default:
		switch (palm_errno)
		{
		    case PALMERR_TIMEOUT:
			cs_errno = CSE_NOCONN;
			break;
		    default:
			break;
		}

		Error(_("Can't read NetSync info."));
		return -1;
	}

	/* Note that we've fetched this information */
	palm->have_netsyncinfo_ = True;

	return 0;		/* Success */
}

/* fetch_userinfo
 * Fetch user info from the Palm.
 */
static int
fetch_userinfo(struct Palm *palm)
{
	int err;

	MISC_TRACE(3)
		fprintf(stderr, "Fetching UserInfo\n");

	/* Get user information from the Palm */
	if ((err = DlpReadUserInfo(palm->pconn_, &(palm->userinfo_))) < 0)
	{
		Error(_("Can't get user info."));

		switch (palm_errno)
		{
		    case PALMERR_TIMEOUT:
			cs_errno = CSE_NOCONN;
			break;

		    default:
			break;
		}

		return -1;
	}

	/* Note that we've fetched this information */
	palm->have_userinfo_ = True;

	MISC_TRACE(3)
	{
		fprintf(stderr, "User info:\n");
		fprintf(stderr, "\tUserID: 0x%08lx\n",
			palm->userinfo_.userid);
		fprintf(stderr, "\tViewerID: 0x%08lx\n",
			palm->userinfo_.viewerid);
		fprintf(stderr, "\tLast sync PC: 0x%08lx (%d.%d.%d.%d)\n",
			palm->userinfo_.lastsyncPC,
			(int) ((palm->userinfo_.lastsyncPC >> 24) & 0xff),
			(int) ((palm->userinfo_.lastsyncPC >> 16) & 0xff),
			(int) ((palm->userinfo_.lastsyncPC >>  8) & 0xff),
			(int) (palm->userinfo_.lastsyncPC & 0xff));
		if (palm->userinfo_.lastgoodsync.year == 0)
		{
			fprintf(stderr, "\tLast good sync: never\n");
		} else {
			fprintf(stderr, "\tLast good sync: %02d:%02d:%02d "
				"%02d/%02d/%02d\n",
				palm->userinfo_.lastgoodsync.hour,
				palm->userinfo_.lastgoodsync.minute,
				palm->userinfo_.lastgoodsync.second,
				palm->userinfo_.lastgoodsync.day,
				palm->userinfo_.lastgoodsync.month,
				palm->userinfo_.lastgoodsync.year);
		}
		if (palm->userinfo_.lastsync.year == 0)
		{
			fprintf(stderr, "\tLast sync attempt: never\n");
		} else {
			fprintf(stderr,
				"\tLast sync attempt: %02d:%02d:%02d "
				"%02d/%02d/%02d\n",
				palm->userinfo_.lastsync.hour,
				palm->userinfo_.lastsync.minute,
				palm->userinfo_.lastsync.second,
				palm->userinfo_.lastsync.day,
				palm->userinfo_.lastsync.month,
				palm->userinfo_.lastsync.year);
		}
		fprintf(stderr, "\tUser name length: %d\n",
			palm->userinfo_.usernamelen);
		fprintf(stderr, "\tUser name: \"%s\"\n",
			palm->userinfo_.username == NULL ?
			"(null)" : palm->userinfo_.username);
		fprintf(stderr, "\tPassword: <%d bytes>\n",
			palm->userinfo_.passwdlen);
		MISC_TRACE(6)
			debug_dump(stderr, "PASS", palm->userinfo_.passwd,
				   palm->userinfo_.passwdlen);
	}

	return 0;		/* Success */
}

/* fetch_serial
 * Fetch the serial number (and its length) from the Palm, if possible. If
 * not, the serial number is set to the empty string, and its length is set
 * to 0.
 */
static int
fetch_serial(struct Palm *palm)
{
	int err;
	int i;
	udword snum_ptr;	/* Palm pointer to serial number */
	uword snum_len;		/* Length of serial number string */
	udword p_rom_version;	/* Palm's ROM version */

	p_rom_version = palm_rom_version(palm);
	if (p_rom_version == 0)
		return -1;

	if (p_rom_version < 0x03000000)
	{
		/* Can't just try to read the serial number and let the RPC
		 * call fail: the PalmPilot(Pro) panics when you do that.
		 */
		SYNC_TRACE(1)
			fprintf(stderr, "This Palm is too old to have a "
				"serial number in ROM\n");

		/* Set the serial number to the empty string */
		palm->serial_[0] = '\0';
		palm->serial_len_ = 0;

		return 0;	/* Success, in a way */
	}

	/* The Palm's ROM is v3.0 or later, so it has a serial number. */

	/* Get the location of the ROM serial number */
	SYNC_TRACE(7)
		fprintf(stderr,
			"Getting location of serial number in ROM.\n");
	/* XXX - Move this into its own function? */
	err = RDLP_ROMToken(palm->pconn_, CARD0, ROMToken_Snum,
			    &snum_ptr, &snum_len);
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

		Error(_("Can't get location of serial number."));
		return -1;
	}
	SYNC_TRACE(7)
		fprintf(stderr, "Serial number is at 0x%08lx, length %d\n",
			snum_ptr, snum_len);

	/* Sanity check: make sure we have space for the serial number. */
	if (snum_len > SNUM_MAX-1)
	{
		Error(_("Warning: ROM serial number is %d characters long. "
			"Please notify the\n"
			"maintainer."),
		      snum_len);
		snum_len = SNUM_MAX;
	}

	/* Read the serial number out of the location found above */
	SYNC_TRACE(7)
		fprintf(stderr, "Reading serial number.\n");
	err = RDLP_MemMove(palm->pconn_, (ubyte *) palm->serial_,
			   snum_ptr, snum_len);
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

		Error(_("Can't read serial number."));
		return -1;
	}
	SYNC_TRACE(7)
		fprintf(stderr, "Serial number is \"%*s\"\n",
			snum_len, palm->serial_);

	palm->serial_[snum_len] = '\0';
	palm->serial_len_ = snum_len;

	/* See if this is one of the special serial numbers defined in
	 * 'special_snums', above.
	 */
	for (i = 0; i < num_special_snums; i++)
	{
		if (snum_len != special_snums[i].real_len)
			continue;		/* Wrong length */

		if (memcmp(palm->serial_, special_snums[i].real, snum_len)
		    != 0)
			continue;		/* Doesn't match */

		/* We have a match. Copy it to 'palm' */
		SYNC_TRACE(5)
			fprintf(stderr, "Found special serial number [%s]\n",
				special_snums[i].alias);
		strncpy(palm->serial_, special_snums[i].alias, SNUM_MAX);
		palm->serial_len_ = strlen(special_snums[i].alias);

		if (palm->serial_len_ > SNUM_MAX-1)
		{
			/* Paranoia. Avoid buffer overflows */
			palm->serial_[SNUM_MAX-1] = '\0';
			palm->serial_len_ = SNUM_MAX-1;
		}

		break;
	}

	return 0;
}

/* ListDBs
 * Fetch the list of database info records from the Palm, both for ROM and
 * RAM.
 */
/* XXX - This needs to be rewritten from scratch */
static int
ListDBs(struct Palm *palm)
{
	int err;
	int card;		/* Memory card number */

	if (palm->num_cards_ < 0)
	{
		err = fetch_meminfo(palm);
		if (err < 0)
			return -1;
	}

	/* Iterate over each memory card */
	for (card = 0; card < palm_num_cards(palm); card++)
	{
		int i;
		ubyte iflags;		/* ReadDBList flags */
		uword start;		/* Database index to start reading
					 * at */
		uword last_index;	/* Index of last database read */
		ubyte oflags;
		ubyte num;

		/* Get total # of databases */
		palm->num_dbs_ =
			palm->cardinfo_[card].ram_dbs;
		if (global_opts.check_ROM)	/* Also considering ROM */
			palm->num_dbs_ += palm->cardinfo_[card].rom_dbs;
		if (palm->num_dbs_ <= 0)
		{
			/* XXX - Fix this */
			Error(_("You have an old Palm, one that "
				"doesn't say how many\n"
				"databases it has. I can't cope with "
				"this."));
			return -1;
		}

		/* Allocate space for the array of database info blocks */
		if ((palm->dblist_ = (struct dlp_dbinfo *)
		     calloc(palm->num_dbs_, sizeof(struct dlp_dbinfo)))
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
		for (i = 0; i < palm->num_dbs_; i++)
		{
			err = DlpReadDBList(palm->pconn_, iflags, card,
					    start, &last_index, &oflags,
					    &num, &(palm->dblist_[i]));
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

				return -1;
			}

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

	/* Record the fact that we've fetched all of the databases */
	palm->have_all_DBs_ = True;

	/* Print out the list of databases, for posterity */
	SYNC_TRACE(2)
	{
		int i;

		fprintf(stderr, "\nDatabase list:\n");
		fprintf(stderr,
			"Name                            flags type crea ver mod. num\n"
			"        ctime                mtime                baktime\n");
		for (i = 0; i < palm->num_dbs_; i++)
		{
			fprintf(stderr,
				"%-*s %04x %c%c%c%c %c%c%c%c %3d %08lx\n",
				PDB_DBNAMELEN,
				palm->dblist_[i].name,
				palm->dblist_[i].db_flags,
				(char) (palm->dblist_[i].type >> 24),
				(char) (palm->dblist_[i].type >> 16),
				(char) (palm->dblist_[i].type >> 8),
				(char) palm->dblist_[i].type,
				(char) (palm->dblist_[i].creator >> 24),
				(char) (palm->dblist_[i].creator >> 16),
				(char) (palm->dblist_[i].creator >> 8),
				(char) palm->dblist_[i].creator,
				palm->dblist_[i].version,
				palm->dblist_[i].modnum);
			fprintf(stderr, "        "
				"%02d:%02d:%02d %02d/%02d/%02d  "
				"%02d:%02d:%02d %02d/%02d/%02d  "
				"%02d:%02d:%02d %02d/%02d/%02d\n",
				palm->dblist_[i].ctime.hour,
				palm->dblist_[i].ctime.minute,
				palm->dblist_[i].ctime.second,
				palm->dblist_[i].ctime.day,
				palm->dblist_[i].ctime.month,
				palm->dblist_[i].ctime.year,
				palm->dblist_[i].mtime.hour,
				palm->dblist_[i].mtime.minute,
				palm->dblist_[i].mtime.second,
				palm->dblist_[i].mtime.day,
				palm->dblist_[i].mtime.month,
				palm->dblist_[i].mtime.year,
				palm->dblist_[i].baktime.hour,
				palm->dblist_[i].baktime.minute,
				palm->dblist_[i].baktime.second,
				palm->dblist_[i].baktime.day,
				palm->dblist_[i].baktime.month,
				palm->dblist_[i].baktime.year);
		}
	}

	return 0;
}

/* palm_rom_version
 * Returns the version of the Palm's ROM, or 0 if case of error.
 */
const udword
palm_rom_version(struct Palm *palm)
{
	int err;

	/* Fetch SysInfo if we haven't done so yet */
	if (!palm->have_sysinfo_)
		if ((err = fetch_sysinfo(palm)) < 0)
			return 0;

	return palm->sysinfo_.rom_version;
}

/* palm_username
 * Returns the username from the Palm.
 */
const char *palm_username(struct Palm *palm)
{
	int err;

	/* Fetch UserInfo if we haven't done so yet */
	if (!palm->have_userinfo_)
	{
		if ((err = fetch_userinfo(palm)) < 0)
			return NULL;
	}

	return palm->userinfo_.username;
}

/* palm_userid
 * Returns the user ID from the Palm.
 * Arguably, the fact that this returns 0 upon error is a Bad Thing, but 0
 * means "this is a fresh Palm", so I have no sympathy for any dumbass who
 * sets it to 0 (e.g., by running ColdSync as root, or something).
 */
const udword palm_userid(struct Palm *palm)
{
	int err;

	/* Fetch UserInfo if we haven't done so yet */
	if (!palm->have_userinfo_)
	{
		if ((err = fetch_userinfo(palm)) < 0)
			return 0;
	}

	return palm->userinfo_.userid;
}

/* palm_viewerid
 * Returns the viewer ID (whateve that is) from the Palm.
 */
const udword palm_viewerid(struct Palm *palm)
{
	int err;

	/* Fetch UserInfo if we haven't done so yet */
	if (!palm->have_userinfo_)
	{
		if ((err = fetch_userinfo(palm)) < 0)
			return 0;
	}

	return palm->userinfo_.viewerid;
}

/* palm_lastsyncPC
 * Returns the ID of the last host that this Palm synced with, or 0 in case
 * of error.
 * XXX - 0 can also mean that the Palm has never synced, so the error value
 * should be something else.
 */
const udword
palm_lastsyncPC(struct Palm *palm)
{
	int err;

	/* Fetch UserInfo if we haven't done so yet */
	if (!palm->have_userinfo_)
	{
		if ((err = fetch_userinfo(palm)) < 0)
			return 0;
	}

	return palm->userinfo_.lastsyncPC;
}

/* palm_serial_len
 * Returns the length of the Palm's serial number.
 */
const int
palm_serial_len(struct Palm *palm)
{
	int err;

	/* Fetch the serial number if we haven't done so yet */
	if (palm->serial_len_ < 0)
	{
		if ((err = fetch_serial(palm)) < 0)
			return -1;
	}

	return palm->serial_len_;
}

/* palm_serial
 * Returns a pointer to the Palm's serial number (a string).
 * The caller is not responsible for freeing this string.
 */
const char *
palm_serial(struct Palm *palm)
{
	int err;

	/* Fetch the serial number if we haven't done so yet */
	if (palm->serial_len_ < 0)
	{
		if ((err = fetch_serial(palm)) < 0)
			return NULL;
	}

	return palm->serial_;
}

/* palm_num_cards
 * Returns the number of memory cards on the Palm.
 */
const int
palm_num_cards(struct Palm *palm)
{
	int err;

	/* Fetch memory info if we haven't done so yet */
	if (palm->num_cards_ < 0)
	{
		if ((err = fetch_meminfo(palm)) < 0)
			return -1;
	}

	return palm->num_cards_;
}

/* palm_fetch_all_DBs
 * Fetch descriptions of all of the databases on the Palm. This is mainly
 * useful for functions which know that they will be dealing with all of
 * the databases on the Palm: this function can fetch and cache them all
 * reasonably quickly. The database iterator functions (palm_resetdb() and
 * palm_nextdb()) may generate more traffic to get the entire list of
 * databases.
 */
int
palm_fetch_all_DBs(struct Palm *palm)
{
	int err;

	MISC_TRACE(6)
		fprintf(stderr, "Inside palm_fetch_all_DBs\n");

	/* Fetch list of databases, if we haven't done so yet */
	if (!palm->have_all_DBs_)
	{
		if ((err = ListDBs(palm)) < 0)
			return -1;
	}

	return 0;	/* Success */
}

/* palm_num_dbs
 * Returns the total number of databases on the Palm.
 */
const int
palm_num_dbs(struct Palm *palm)
{
	int err;

	/* Fetch list of databases, if we haven't done so yet */
	if (!palm->have_all_DBs_)
	{
		if ((err = ListDBs(palm)) < 0)
			return -1;
	}

	return palm->num_dbs_;
}

/* palm_resetdb
 * Reset the 'struct Palm's database iterator so that the next call to
 * palm_nextdb() will return the first database.
 */
void
palm_resetdb(struct Palm *palm)
{
	MISC_TRACE(6)
		fprintf(stderr, "Resetting Palm database iterator.\n");

	palm->dbit_ = 0;
}

/* palm_nextdb
 * Returns the next database in the 'struct Palm's list of databases. This
 * is intended to be used in loops that iterate over some or all of the
 * databases on the Palm, e.g.:
 *	struct Palm *palm;
 *	const struct dlp_dbinfo *db;
 *
 *	palm_resetdb(palm);
 *	while ((db = palm_nextdb(palm)) != NULL)
 *		...
 *
 * When the internal iterator reaches the end of the list of databases,
 * palm_nextdb() returns NULL.
 */
const struct dlp_dbinfo *
palm_nextdb(struct Palm *palm)
{
	int err;
	const struct dlp_dbinfo *retval;
	int num_dbs;			/* # databases on Palm */

	MISC_TRACE(6)
		fprintf(stderr, "Palm database iterator++\n");

	/* XXX - This is a naive implementation. It can be improved: this
	 * function should fetch as few databases as possible, to save
	 * time.
	 * "As few as possible" means one, in the simple implementation.
	 * PalmOS <mumble> allows one to fetch more than one database at a
	 * time. This is probably the best thing to do. Or perhaps
	 * palm_fetch_all_DBs() should fetch multiple databases at a time
	 * (if possible), and palm_nextdb() should content itself with
	 * fetching only one at a time.
	 * Presumably, if the caller knows that it will be iterating over
	 * all databases, it will call palm_fetch_all_DBs().
	 */
	/* Fetch list of databases, if we haven't done so yet */
	if (!palm->have_all_DBs_)
	{
		if ((err = ListDBs(palm)) < 0)
			return NULL;
	}

	/* Find out how many databases there are */
	num_dbs = palm_num_dbs(palm);
	if (num_dbs < 0)
		return NULL;

	/* Have we reached the end of the list yet? */
	if (palm->dbit_ > num_dbs - 1)
	{
		MISC_TRACE(6)
			fprintf(stderr, "Reached end of list\n");
		return NULL;	/* Reached end of list */
	}

	/* Haven't reached the end of the list yet */
	MISC_TRACE(6)
		fprintf(stderr, "Returning database %d\n", palm->dbit_);

	retval = &(palm->dblist_[palm->dbit_]);
	palm->dbit_++;
	return retval;
}

/* palm_find_dbentry
 * Look through the list of databases in 'palm' and try to find the one
 * named 'name'. Returns a pointer to its entry in 'palm->dblist' if it
 * exists, or NULL otherwise.
 */
const struct dlp_dbinfo *
palm_find_dbentry(struct Palm *palm,
		  const char *name)
{
	const struct dlp_dbinfo *cur_db;	/* Database iterator */

	palm_resetdb(palm);
	while ((cur_db = palm_nextdb(palm)) != NULL)
	{
		if (strncmp(name, cur_db->name,
			    DLPCMD_DBNAME_LEN) == 0)
			/* Found it */
			return cur_db;
	}

	return NULL;		/* Couldn't find it */
}

/* palm_append_dbentry
 * Append an entry for 'pdb' to palm->dblist.
 */
int
palm_append_dbentry(struct Palm *palm,
		    struct pdb *pdb)
{
	struct dlp_dbinfo *newdblist;
	struct dlp_dbinfo *dbinfo;
	int num_dbs;			/* # databases on Palm */

	MISC_TRACE(4)
		fprintf(stderr, "palm_append_dbentry: adding \"%s\"\n",
			pdb->name);

	/* Find out how many databases there are */
	num_dbs = palm_num_dbs(palm);
	if (num_dbs < 0)
		return -1;

	/* Resize the existing 'dblist'. */
	newdblist = realloc(palm->dblist_,
			    (num_dbs + 1) *
			    sizeof(struct dlp_dbinfo));
	if (newdblist == NULL)
	{
		Error(_("Can't resize palm->dblist."));
		return -1;
	}
	palm->dblist_ = newdblist;
	palm->num_dbs_++;

	/* Fill in the new entry */
	dbinfo = &(palm->dblist_[num_dbs]);
	return dbinfo_fill(dbinfo, pdb);
}

const char *
palm_netsync_hostname(struct Palm *palm)
{
	int err;

	/* Fetch NetSync info if we haven't done so yet */
	if (!palm->have_netsyncinfo_)
	{
		if ((err = fetch_netsyncinfo(palm)) < 0)
			return NULL;
	}

	return palm->netsyncinfo_.hostname;
}

const char *
palm_netsync_hostaddr(struct Palm *palm)
{
	int err;

	/* Fetch NetSync info if we haven't done so yet */
	if (!palm->have_netsyncinfo_)
	{
		if ((err = fetch_netsyncinfo(palm)) < 0)
			return NULL;
	}

	return palm->netsyncinfo_.hostaddr;
}

const char *
palm_netsync_netmask(struct Palm *palm)
{
	int err;

	/* Fetch NetSync info if we haven't done so yet */
	if (!palm->have_netsyncinfo_)
	{
		if ((err = fetch_netsyncinfo(palm)) < 0)
			return NULL;
	}

	return palm->netsyncinfo_.hostnetmask;
}

/* This is for Emacs's benefit:
 * Local Variables:	***
 * fill-column:	75	***
 * End:			***
 */
