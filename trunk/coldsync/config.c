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
 * $Id: config.c,v 1.1 1999-03-11 03:38:44 arensb Exp $
 */
#include <stdio.h>
#include <unistd.h>		/* For getuid() */
#include <sys/types.h>		/* For getuid(), getpwuid() */
#include <pwd.h>		/* For getpwuid() */
#include <sys/param.h>		/* For MAXPATHLEN */
#include "coldsync.h"
#include "pconn/dlp_cmd.h"

char syncdir[MAXPATHLEN];	/* Directory to sync with */

/* load_config
 * Read the configuration for this instance of 'coldsync'.
 */
int
load_config(int argc,		/* Command-line arguments */
	    char *argv[])
{
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
	uid_t uid;		/* UID of user running 'coldsync' */
				/* XXX - Different OSes' getuid() return
				 * different types. Deal with this in
				 * 'autoconf'
				 */
	struct passwd *pwent;	/* /etc/passwd entry for current user */
/*  	char conf_fname[MAXPATHLEN]; */	/* XXX */
				/* Name of per-user config file */

	/* Get the current user's UID */
	if ((uid = getuid()) < 0)
	{
		perror("load_palm_config: getuid");
		return -1;
	}
fprintf(stderr, "UID: %u\n", uid);

	/* Get the user's password file info */
	if ((pwent = getpwuid(uid)) == NULL)
	{
		perror("load_palm_config: getpwuid");
		return -1;
	}
fprintf(stderr, "pwent:\n");
fprintf(stderr, "\tpw_name: \"%s\"\n", pwent->pw_name);
fprintf(stderr, "\tpw_uid: %u\n", pwent->pw_uid);
fprintf(stderr, "\tpw_gecos: \"%s\"\n", pwent->pw_gecos);
fprintf(stderr, "\tpw_dir: \"%s\"\n", pwent->pw_dir);

	return 0; 
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
