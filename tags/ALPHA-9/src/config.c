/* config.c
 *
 * Functions dealing with loading the configuration.
 *
 * XXX - This is all rather rough and unfinished, mainly because I'm
 * not entirely sure how to do things. For now, I'm (sorta) assuming
 * one user, one Palm, one machine, but this is definitely going to
 * change: a user might have several Palms; a machine can sync any
 * Palm; and, of course, a machine has any number of users.
 * Hence, the configuration is (will be) somewhat complicated.
 *
 * $Id: config.c,v 1.2 1999-07-12 09:32:23 arensb Exp $
 */
#include <stdio.h>
#include <unistd.h>		/* For getuid(), gethostname() */
#include <sys/types.h>		/* For getuid(), getpwuid() */
#include <sys/stat.h>		/* For stat() */
#include <pwd.h>		/* For getpwuid() */
#include <sys/param.h>		/* For MAXPATHLEN */
#include <netdb.h>		/* For gethostbyname() */
#include <sys/socket.h>		/* For AF_INET */
#include <string.h>		/* For string functions */
#include <ctype.h>		/* For toupper() */
#include "coldsync.h"
#include "dlp_cmd.h"

/* XXX - For debugging only */
extern void debug_dump(FILE *outfile, const char *prefix,
		       const ubyte *buf, const udword len);

/* XXX - This should probably be hidden inside a "struct config{..}" or
 * something. I don't like global variables.
 */
udword hostid;			/* This machine's host ID, so you can tell
				 * whether this was the last machine the
				 * Palm synced with.
				 */
uid_t user_uid;
char user_fullname[DLPCMD_USERNAME_LEN];
char palmdir[MAXPATHLEN+1];	/* ~/.palm pathname */
char backupdir[MAXPATHLEN+1];	/* ~/.palm/backup pathname */
char archivedir[MAXPATHLEN+1];	/* ~/.palm/archive pathname */
char installdir[MAXPATHLEN+1];	/* ~/.palm/install pathname */

static int get_fullname(char *buf, const int buflen,
			const struct passwd *pwent);

/* load_config
 * Read the configuration for this instance of 'coldsync'.
 */
int
load_config()
{
	int err;
	int i;
	static char hostname[MAXHOSTNAMELEN+1];	/* Buffer to hold this
						 * host's name. */
	struct hostent *myaddr;

	/* By default, the host ID is its IP address. */
	/* XXX - This should probably be configurable */
	/* Get the hostname */
	if ((err = gethostname(hostname, MAXHOSTNAMELEN)) < 0)
	{
		fprintf(stderr, "Can't get host name\n");
		perror("gethostname");
		return -1;
	}
	MISC_TRACE(2)
		fprintf(stderr, "My name is \"%s\"\n", hostname);

	/* Look up the hostname */
	if ((myaddr = gethostbyname(hostname)) == NULL)
	{
		fprintf(stderr, "Can't look up my address\n");
		perror("gethostbyname");
		return -1;
	}
	MISC_TRACE(2)
	{
		fprintf(stderr, "My canonical name is \"%s\"\n",
			myaddr->h_name);
		fprintf(stderr, "My aliases are:\n");
		for (i = 0; myaddr->h_aliases[i] != NULL; i++)
		{
			fprintf(stderr, "    %d: \"%s\"\n", i,
				myaddr->h_aliases[i]);
		}
		fprintf(stderr, "My address type is %d\n", myaddr->h_addrtype);
	}

	/* XXX - There should probably be functions to deal with other
	 * address types (e.g., IPv6). Maybe just hash them down to 4
	 * bytes. Hm... actually, that might work for all address types, so
	 * no need to test for AF_INET specifically.
	 */
	if (myaddr->h_addrtype != AF_INET)
	{
		fprintf(stderr, "Hey! This isn't an AF_INET address!\n");
		return -1;
	} 

	MISC_TRACE(2)
	{
		fprintf(stderr, "My address length is %d\n", myaddr->h_length);
		fprintf(stderr, "My addresses are:\n");
		for (i = 0; myaddr->h_addr_list[i] != NULL; i++)
		{
			fprintf(stderr, "    Address %d:\n", i);
			debug_dump(stderr, "ADDR",
				   (const ubyte *) myaddr->h_addr_list[i],
				   myaddr->h_length);
		}
	}

	/* Make sure there's at least one address */
	if (myaddr->h_addr_list[0] == NULL)
	{
		fprintf(stderr, "This host doesn't appear to have an IP address.\n");
		return -1;
	}

	/* Use the first address as the host ID */
	hostid = (((udword) myaddr->h_addr_list[0][0] & 0xff) << 24) |
		(((udword) myaddr->h_addr_list[0][1] & 0xff) << 16) |
		(((udword) myaddr->h_addr_list[0][2] & 0xff) << 8) |
		((udword) myaddr->h_addr_list[0][3] & 0xff);
	MISC_TRACE(2)
		fprintf(stderr, "My hostid is 0x%08lx\n", hostid);

	return 0;
}

int
load_palm_config(struct Palm *palm)
{
	/* XXX - For now, this assumes that 'coldsync' runs as a user app,
	 * i.e., it's started by the user at login time (or 'startx' time).
	 * This simplifies things, in that we can just use getuid() to get
	 * the Palm owner's uid.
	 * Eventually, what should happen is this: 'coldsync' will become a
	 * daemon. It'll use the information from ReadUserInfo and/or
	 * ReadSysInfo to determine which Palm it's talking to, look this
	 * information up in a table (or something), and determine which
	 * directory it needs to sync with, where to get the per-Palm
	 * configuration, and so forth.
	 */
	int err;
	uid_t uid;		/* UID of user running 'coldsync' */
				/* XXX - Different OSes' getuid() return
				 * different types. Deal with this in
				 * 'autoconf'
				 */
	struct passwd *pwent;	/* /etc/passwd entry for current user */
	struct stat statbuf;	/* For checking for files and directories */

	/* Get the current user's UID */
	if ((uid = getuid()) < 0)
	{
		perror("load_palm_config: getuid");
		return -1;
	}
	MISC_TRACE(2)
		fprintf(stderr, "UID: %u\n", uid);

	/* Get the user's password file info */
	if ((pwent = getpwuid(uid)) == NULL)
	{
		perror("load_palm_config: getpwuid");
		return -1;
	}
	MISC_TRACE(2)
	{
		fprintf(stderr, "pwent:\n");
		fprintf(stderr, "\tpw_name: \"%s\"\n", pwent->pw_name);
		fprintf(stderr, "\tpw_uid: %u\n", pwent->pw_uid);
		fprintf(stderr, "\tpw_gecos: \"%s\"\n", pwent->pw_gecos);
		fprintf(stderr, "\tpw_dir: \"%s\"\n", pwent->pw_dir);
	}

	user_uid = pwent->pw_uid;	/* Get the user's UID */
 
	/* Copy the user's full name to 'user_fullname' */
	if (get_fullname(user_fullname, DLPCMD_USERNAME_LEN, pwent) < 0)
	{
		fprintf(stderr, "Can't get user's full name\n");
		return -1;
	}
	MISC_TRACE(2)
		fprintf(stderr, "Full name: \"%s\"\n", user_fullname);

	/* Make sure the various directories (~/.palm/...) exist, and create
	 * them if necessary.
	 */
	if (stat(pwent->pw_dir, &statbuf) < 0)
	{
		/* Home directory doesn't exist. Not much we can do about
		 * that.
		 */
		perror("load_palm_config: stat($HOME)");
		return -1;
	}

	/* Construct the various directory paths */
	/* XXX - By default, the backup directory should be of the form
	 * ~<user>/.palm/<palm ID>/backup or something. <palm ID> should be
	 * the serial number for a Palm III, not sure what for others.
	 * Perhaps 'ColdSync' could create a resource database with this
	 * information?
	 * XXX - For now, let it be ~/.palm/backup
	 */

	/* ~/.palm */
	strncpy(palmdir, pwent->pw_dir, MAXPATHLEN);
	strncat(palmdir, "/.palm", MAXPATHLEN - strlen(palmdir));

	if (stat(palmdir, &statbuf) < 0)
	{
		/* ~/.palm doesn't exist. Create it */
		/* XXX - The directory mode ought to be configurable */
		if ((err = mkdir(palmdir, 0700)) < 0)
		{
			/* Can't create the directory */
			perror("load_palm_config: mkdir(~/.palm)\n");
			return -1;
		}
	}

	/* XXX - At this point, we should read a config file in ~/.palm,
	 * see where it says the backup, archive, install directories are,
	 * and make sure those exist. The paths below are just overridable
	 * defaults.
	 */

	/* ~/.palm/backup */
	strncpy(backupdir, palmdir, MAXPATHLEN);
	strncat(backupdir, "/backup", MAXPATHLEN - strlen(palmdir));

	if (stat(backupdir, &statbuf) < 0)
	{
		/* ~/.palm/backup doesn't exist. Create it */
		/* XXX - The directory mode ought to be configurable */
		if ((err = mkdir(backupdir, 0700)) < 0)
		{
			/* Can't create the directory */
			perror("load_palm_config: mkdir(~/.palm/backup)\n");
			return -1;
		}
	}

	/* ~/.palm/archive */
	strncpy(archivedir, palmdir, MAXPATHLEN);
	strncat(archivedir, "/archive", MAXPATHLEN - strlen(palmdir));

	if (stat(archivedir, &statbuf) < 0)
	{
		/* ~/.palm/archive doesn't exist. Create it */
		/* XXX - The directory mode ought to be configurable */
		if ((err = mkdir(archivedir, 0700)) < 0)
		{
			/* Can't create the directory */
			perror("load_palm_config: mkdir(~/.palm/archive)\n");
			return -1;
		}
	}

	/* ~/.palm/install */
	strncpy(installdir, palmdir, MAXPATHLEN);
	strncat(installdir, "/install", MAXPATHLEN - strlen(palmdir));

	if (stat(installdir, &statbuf) < 0)
	{
		/* ~/.palm/install doesn't exist. Create it */
		/* XXX - The directory mode ought to be configurable */
		if ((err = mkdir(installdir, 0700)) < 0)
		{
			/* Can't create the directory */
			perror("load_palm_config: mkdir(~/.palm/install)\n");
			return -1;
		}
	}

	return 0; 
}

/* get_fullname
 * Extracts the user's full name from 'pwent's GECOS field. Expand '&'
 * correctly. Puts the name in 'buf'; 'buflen' is the length of 'buf',
 * counting the terminating NUL.
 * Returns 0 if successful, -1 otherwise.
 * The GECOS field consists of comma-separated fields (full name, location,
 * office phone, home phone, other). The full name may contain an ampersand
 * ("&"). This is replaced by the username, capitalized.
 */
static int
get_fullname(char *buf,
	     const int buflen,
	     const struct passwd *pwent)
{
	int bufi;		/* Index into 'buf' */
	int gecosi;		/* Index into GECOS field */
	int namei;		/* Index into username field */

	for (bufi = 0, gecosi = 0; bufi < buflen-1; gecosi++)
	{
		switch (pwent->pw_gecos[gecosi])
		{
		    case ',':		/* End of subfield */
		    case '\0':		/* End of GECOS field */
			goto done;
		    case '&':
			/* Expand '&' */
			/* XXX - egcs whines about "ANSI C forbids
			 * braced-groups within expressions".
			 */
			buf[bufi] = toupper(pwent->pw_name[0]);
			bufi++;
			for (namei = 1; pwent->pw_name[namei] != '\0';
			     bufi++, namei++)
			{
				if (bufi >= buflen-1)
					goto done;
				buf[bufi] = pwent->pw_name[namei];
			}
			break;
		    default:
			buf[bufi] = pwent->pw_gecos[gecosi];
			bufi++;
			break;
					/* Copy next character */
		}
	}
  done:
	buf[bufi] = '\0';

	return 0;
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
