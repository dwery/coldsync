/* config.c
 *
 * Functions dealing with loading the configuration.
 *
 *	Copyright (C) 1999, 2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * XXX - This is all rather rough and unfinished, mainly because I'm
 * not entirely sure how to do things. For now, I'm (sorta) assuming
 * one user, one Palm, one machine, but this is definitely going to
 * change: a user might have several Palms; a machine can sync any
 * Palm; and, of course, a machine has any number of users.
 * Hence, the configuration is (will be) somewhat complicated.
 *
 * $Id: config.c,v 1.22 2000-04-10 09:57:24 arensb Exp $
 */
#include "config.h"
#include <stdio.h>
#include <unistd.h>		/* For getuid(), gethostname() */
#include <stdlib.h>		/* For atoi(), getenv() */
#include <sys/types.h>		/* For getuid(), getpwuid() */
#include <sys/stat.h>		/* For stat() */
#include <pwd.h>		/* For getpwuid() */
#include <sys/param.h>		/* For MAXPATHLEN */
#include <netdb.h>		/* For gethostbyname() */
#include <sys/socket.h>		/* For AF_INET */
#include <string.h>		/* For string functions */
#include <ctype.h>		/* For toupper() */

#if HAVE_LIBINTL
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL */

#include "coldsync.h"
#include "pconn/pconn.h"
#include "parser.h"		/* For config file parser stuff */

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN	256
#endif	/* MAXHOSTNAMELEN */

#define PALMDEV		"/dev/palm"	/* Default device */
#define DIR_MODE	0700		/* Default permissions for new
					 * directories. */

extern struct config config;

/* For debugging only */
extern void debug_dump(FILE *outfile, const char *prefix,
		       const ubyte *buf, const udword len);

int sys_maxfds;			/* Size of file descriptor table */

/* XXX - This should probably be hidden inside a "struct config{..}" or
 * something. I don't like global variables.
 */
udword hostid;			/* This machine's host ID, so you can tell
				 * whether this was the last machine the
				 * Palm synced with.
				 */
uid_t user_uid;
char palmdir[MAXPATHLEN+1];	/* ~/.palm pathname */
char backupdir[MAXPATHLEN+1];	/* ~/.palm/backup pathname */
char atticdir[MAXPATHLEN+1];	/* ~/.palm/backup/Attic pathname */
char archivedir[MAXPATHLEN+1];	/* ~/.palm/archive pathname */
char installdir[MAXPATHLEN+1];	/* ~/.palm/install pathname */

struct userinfo userinfo;	/* Information about the Palm's owner */

static int name2listen_type(const char *str);
static int get_fullname(char *buf, const int buflen,
			const struct passwd *pwent);
static int get_userinfo(struct userinfo *userinfo);
static int get_maxfds(void);

/* get_maxfds
 * Return the size of the file descriptor table, using whichever method is
 * available.
 */
#if HAVE_SYSCONF

static int
get_maxfds(void)
{
	return sysconf(_SC_OPEN_MAX);
}

#else	/* !HAVE_SYSCONF */
#  error "Don't know how to get size of file descriptor table."
#endif	/* HAVE_SYSCONF */

/* get_config
 * Get the initial configuration: parse command-line arguments and load the
 * configuration file, if any.
 * For now, this assumes standalone mode, not daemon mode.
 */
int
get_config(int argc, char *argv[])
{
	int err;
	int oldoptind;			/* Previous value of 'optind', to
					 * allow us to figure out exactly
					 * which argument was bogus, and
					 * thereby print descriptive error
					 * messages. */
	int arg;			/* Current option */
	char *config_fname = NULL;	/* Name of config file to read */
	static char fname_buf[MAXPATHLEN];
					/* Buffer in which to construct the
					 * config file path, if not given.
					 */
	Bool config_fname_given;	/* Whether the user specified a
					 * config file on the command line.
					 */
	struct stat statbuf;		/* For stat() */
	char *devname = NULL;		/* Name of device to listen on */
	int devtype = -1;		/* type of device to listen on */

	struct config *user_config = NULL;
					/* Configuration read from config
					 * file (as opposed to what was
					 * specified on the command line).
					 */
	static char hostname[MAXHOSTNAMELEN+1];	/* Buffer to hold this
						 * host's name. */
	struct hostent *myaddr;

	/* Initialize the global options to sane values */
	global_opts.do_backup		= False;
	global_opts.backupdir		= NULL;
	global_opts.do_restore		= False;
	global_opts.restoredir		= NULL;
	global_opts.force_install	= False;

	/* Initialize the debugging levels to 0 */
	slp_trace	= 0;
	cmp_trace	= 0;
	padp_trace	= 0;
	dlp_trace	= 0;
	dlpc_trace	= 0;
	sync_trace	= 0;
	pdb_trace	= 0;
	misc_trace	= 0;
	io_trace	= 0;

	oldoptind = optind;		/* Initialize "last argument"
					 * index.
					 */

	sys_maxfds = get_maxfds();	/* Get the size of the file
					 * descriptor table.
					 */

	/* By default, the host ID is its IP address. */
	/* Get the hostname */
	if ((err = gethostname(hostname, MAXHOSTNAMELEN)) < 0)
	{
		fprintf(stderr, _("Can't get host name\n"));
		perror("gethostname");
		return -1;
	}

	/* Look up the hostname */
	if ((myaddr = gethostbyname(hostname)) == NULL)
	{
		fprintf(stderr, _("Can't look up my address\n"));
		perror("gethostbyname");
		return -1;
	}

	/* XXX - There should probably be functions to deal with other
	 * address types (e.g., IPv6). Maybe just hash them down to 4
	 * bytes. Hm... actually, that might work for all address types, so
	 * no need to test for AF_INET specifically.
	 */
	if (myaddr->h_addrtype != AF_INET)
	{
		fprintf(stderr, _("Hey! This isn't an AF_INET address!\n"));
		return -1;
	} 

	/* Make sure there's at least one address */
	if (myaddr->h_addr_list[0] == NULL)
	{
		fprintf(stderr,
			_("This host doesn't appear to have an "
			  "IP address.\n"));
		return -1;
	}

	/* Use the first address as the host ID */
	hostid = (((udword) myaddr->h_addr_list[0][0] & 0xff) << 24) |
		(((udword) myaddr->h_addr_list[0][1] & 0xff) << 16) |
		(((udword) myaddr->h_addr_list[0][2] & 0xff) << 8) |
		((udword) myaddr->h_addr_list[0][3] & 0xff);
	MISC_TRACE(2)
		fprintf(stderr, "My hostid is 0x%08lx\n", hostid);

	/* XXX - Any other system-dependent values that should be
	 * determined at runtime?
	 */

	/* Start by reading command-line options. */
	config_fname_given = False;
	while ((arg = getopt(argc, argv, ":hVSFRIf:b:r:p:t:d:")) != -1)
	{
		switch (arg)
		{
		    case 'h':	/* -h: Print usage message and exit */
			usage(argc,argv);
			return -1;	/* XXX - Returning -1 causes main()
					 * to print an error message. This
					 * shouldn't happen.
					 */

		    case 'V':	/* -V: Print version number and exit */
			print_version();
			return -1;

		    case 'S':	/* -S: Force slow sync */
			global_opts.force_slow = True;
			break;

		    case 'F':	/* -F: Force fast sync */
			global_opts.force_fast = True;
			break;

		    case 'R':	/* -R: Consider ROM databases */
			global_opts.check_ROM = True;
			break;

		    case 'I':	/* -I: Install younger databases */
			global_opts.force_install = True;
			break;

		    case 'f':	/* -f <file>: Read configuration from
				 * <file>.
				 */
			config_fname = optarg;
			config_fname_given = True;
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
			devname = optarg;
			break;

		    case 't':	/* -t <device type>: Listen on port device
				 * of type <device type>
				 */
			devtype = name2listen_type(optarg);
			if (devtype < 0)
			{
				fprintf(stderr,
					_("Unknown device type: \"%s\"\n"),
					optarg);
				usage(argc, argv);
				return -1;
			}
			break;

		    case 'd':	/* -d <fac>:<n>: Debugging level */
			set_debug_level(optarg);
			break;

		    case '?':	/* Unknown option */
			fprintf(stderr, _("Unrecognized option: \"%s\"\n"),
				argv[oldoptind]);
			usage(argc, argv);
			return -1;

		    case ':':	/* An argument required an option, but none
				 * was given (e.g., "-u" instead of "-u
				 * daemon").
				 */
			fprintf(stderr, _("Missing option argument after "
					  "\"%s\"\n"),
				argv[oldoptind]);
			usage(argc, argv);
			return -1;

		    default:
			fprintf(stderr,
				_("You specified an apparently legal option "
				  "(\"-%c\"), but I don't know what\n"
				  "to do with it. This is a bug. Please "
				  "notify the maintainer.\n"),
				arg);
			return -1;
			break;
		}
	}

	MISC_TRACE(6)
		/* This really belongs earlier in this function, but the
		 * -dmisc flag hasn't been parsed then.
		 */
		fprintf(stderr, "sys_maxfds == %d\n", sys_maxfds);
	MISC_TRACE(2)
	{
		int i;

		fprintf(stderr, "My name is \"%s\"\n", hostname);
		fprintf(stderr, _("My canonical name is \"%s\"\n"),
			myaddr->h_name);
		fprintf(stderr, "My aliases are:\n");
		for (i = 0; myaddr->h_aliases[i] != NULL; i++)
		{
			fprintf(stderr, "    %d: \"%s\"\n", i,
				myaddr->h_aliases[i]);
		}
		fprintf(stderr, "My address type is %d\n", myaddr->h_addrtype);

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

	/* XXX - Check for trailing arguments. If they're of the form
	 * "FOO=bar", set the variable $FOO to value "bar". Otherwise,
	 * complain and exit.
	 */

	/* Sanity checks */

	/* Can't back up and restore at the same time */
	if (global_opts.do_backup &&
	    global_opts.do_restore)
	{
		fprintf(stderr, _("Error: Can't specify backup and restore "
				  "at the same time.\n"));
		usage(argc, argv);
		return -1;
	}

	/* Can't force both a slow and a fast sync */
	if (global_opts.force_slow &&
	    global_opts.force_fast)
	{
		fprintf(stderr, _("Error: Can't force slow and fast sync "
				  "at the same time.\n"));
		usage(argc, argv);
		return -1;
	}

	/* XXX - If running in daemon mode, don't run this here. Wait for a
	 * connection, fork(), setuid(), and _then_ run get_userinfo().
	 */
	if (get_userinfo(&userinfo) < 0)
	{
		fprintf(stderr, _("Can't get user info\n"));
		return -1;
	}

	/* Config file: if a config file was specified on the command line,
	 * use that. Otherwise, default to ~/.coldsyncrc.
	 */
	/* XXX - Ought to see if /etc/coldsync.common (or whatever) exists;
	 * if it does, get site-wide defaults from there. The user's config
	 * file overrides (some of) that, and command-line arguments
	 * override (almost) everything, naturally.
	 * The exception is that the sysadmin must be able to forbid
	 * certain things, e.g., ve may mandate a set of conduits.
	 */
	if (config_fname == NULL)
	{
		/* No config file specified on the command line. Construct
		 * the full pathname to ~/.coldsyncrc in 'fname_buf'.
		 */
		strncpy(fname_buf, userinfo.homedir, MAXPATHLEN);
		strncat(fname_buf, "/.coldsyncrc",
			MAXPATHLEN - strlen(fname_buf));
		config_fname = fname_buf;
	}
	MISC_TRACE(2)
		fprintf(stderr, "Reading configuration from \"%s\"\n",
			config_fname);

	/* Allocate a place to put the user's configuration */
	if ((user_config = new_config()) == NULL)
	{
		fprintf(stderr, _("Can't allocate new configuration.\n"));
		return -1;
	}

	/* Make sure this file exists. If it doesn't, fall back on
	 * defaults.
	 */
	if (stat(config_fname, &statbuf) < 0)
	{
		/* The file doesn't exist */
		if (config_fname_given)
		{
			/* The user explicitly said to use this file, but
			 * it doesn't exist. Give a warning.
			 */
			fprintf(stderr, _("Warning: config file \"%s\" "
				"doesn't exist. Using defaults.\n"),
				config_fname);
		}
		config_fname = NULL;
	}

	if (config_fname != NULL)
	{
		/* Config file exists. Read it */
		if (parse_config(config_fname, user_config) < 0)
		{
			fprintf(stderr, _("Error reading configuration file "
				"\"%s\"\n"),
				config_fname);
			free_config(user_config);
			return -1;
		}
	}

	/* Fill in the fields for the main configuration */
	config.mode = Standalone;

	/* If the user specified a device on the command line, build a
	 * listen block for it. Otherwise, use the list given in the config
	 * file.
	 */
	if (devname != NULL)
	{
		/* Use the device specified on the command line. */
		listen_block *l;

		MISC_TRACE(4)
			fprintf(stderr,
				"Device specified on command line: \"%s\"\n", 
				devname);

		if ((l = new_listen_block()) == NULL)
		{
			fprintf(stderr, _("Can't allocate listen block.\n"));
			free_config(user_config);
			return -1;
		}

		if ((l->device = strdup(devname)) == NULL)
		{
			fprintf(stderr, _("Can't copy string.\n"));
			free_listen_block(l);
			free_config(user_config);
			return -1;
		}

		if (devtype >= 0)
			l->listen_type = devtype;

		/* Make the new listen block be the one for the main
		 * configuration.
		 */
		config.listen = l;
		l = NULL;
	} else {
		/* No device specified on the command line. Use the one(s)
		 * from the config file.
		 */
		MISC_TRACE(4)
			fprintf(stderr, "No device specified on the "
				"command line.\n");

		config.listen = user_config->listen;
		user_config->listen = NULL;

		/* If the config file didn't specify any listen blocks,
		 * fall back on the default.
		 */
		if (config.listen == NULL)
		{
			listen_block *l;

			MISC_TRACE(4)
				fprintf(stderr, "No device specified on the "
					"command line or in config file.\n"
					"Using default: \""
					PALMDEV "\"\n");

			if ((l = new_listen_block()) == NULL)
			{
				fprintf(stderr,
					_("Can't allocate listen block.\n"));
				free_config(user_config);
				return -1;
			}

			if ((l->device = strdup(PALMDEV)) == NULL)
			{
				fprintf(stderr, _("Can't copy string.\n"));
				free_listen_block(l);
				free_config(user_config);
				return -1;
			}

			/* Make the new listen block be the one for the
			 * main configuration.
			 */
			config.listen = l;
			l = NULL;
		}
	}

	/* Set up the list of PDAs in the main configuration */
	config.pda = user_config->pda;
	user_config->pda = NULL;

	/* Set up the conduit lists in the main configuration */
	if (user_config == NULL)
	{
		config.sync_q		= NULL;
		config.fetch_q		= NULL;
		config.dump_q		= NULL;
		config.install_q	= NULL;
		config.uninstall_q	= NULL;
	} else {
		config.sync_q		= user_config->sync_q;
		user_config->sync_q	= NULL;
		config.fetch_q		= user_config->fetch_q;
		user_config->fetch_q	= NULL;
		config.dump_q		= user_config->dump_q;
		user_config->dump_q	= NULL;
		config.install_q	= user_config->install_q;
		user_config->install_q	= NULL;
		config.uninstall_q	= user_config->uninstall_q;
		user_config->uninstall_q	= NULL;
	}

	SYNC_TRACE(4)
	{
		/* Dump a summary of the config file */
		listen_block *l;
		pda_block *p;
		conduit_block *c;

		fprintf(stderr, "Summary of sync configuration:\n");
		for (l = config.listen; l != NULL; l = l->next)
		{
			fprintf(stderr, "Listen:\n");
			fprintf(stderr, "\tType: %d\n", l->listen_type);
			fprintf(stderr, "\tDevice: [%s]\n", l->device);
			fprintf(stderr, "\tSpeed: %d\n", l->speed);
		}

		fprintf(stderr, "Known PDAs:\n");
		for (p = config.pda; p != NULL; p = p->next)
		{
			fprintf(stderr, "PDA:\n");
			fprintf(stderr, "\tSerial number: [%s]\n",
				(p->snum == NULL ? "(null)" : p->snum));
			fprintf(stderr, "\tDirectory: [%s]\n",
				(p->directory == NULL ? "(null)" :
				 p->directory));
			if ((p->flags & PDAFL_DEFAULT) != 0)
				fprintf(stderr, "\tDEFAULT\n");
		}

		fprintf(stderr, "Sync conduits:\n");
		for (c = config.sync_q; c != NULL; c = c->next)
		{
			fprintf(stderr, "  Conduit:\n");
			if (c->flavor != Sync)
				fprintf(stderr, "Error: wrong conduit flavor. "
					"Expected %d (Sync), but this is %d\n",
					Sync, c->flavor);
			fprintf(stderr, "\tCreator: [%c%c%c%c] 0x%08lx\n",
				(char) ((c->dbcreator >> 24) & 0xff),
				(char) ((c->dbcreator >> 16) & 0xff),
				(char) ((c->dbcreator >> 8) & 0xff),
				(char) (c->dbcreator & 0xff),
				c->dbcreator);
			fprintf(stderr, "\tType: [%c%c%c%c] 0x%08lx\n",
				(char) ((c->dbtype >> 24) & 0xff),
				(char) ((c->dbtype >> 16) & 0xff),
				(char) ((c->dbtype >> 8) & 0xff),
				(char) (c->dbtype & 0xff),
				c->dbtype);
			fprintf(stderr, "\tPath: [%s]\n", c->path);
		}
		
		fprintf(stderr, "Fetch conduits:\n");
		for (c = config.fetch_q; c != NULL; c = c->next)
		{
			fprintf(stderr, "  Conduit:\n");
			if (c->flavor != Fetch)
				fprintf(stderr, "Error: wrong conduit flavor. "
					"Expected %d (Fetch), but this is %d\n",
					Fetch, c->flavor);
			fprintf(stderr, "\tCreator: [%c%c%c%c] 0x%08lx\n",
				(char) ((c->dbcreator >> 24) & 0xff),
				(char) ((c->dbcreator >> 16) & 0xff),
				(char) ((c->dbcreator >> 8) & 0xff),
				(char) (c->dbcreator & 0xff),
				c->dbcreator);
			fprintf(stderr, "\tType: [%c%c%c%c] 0x%08lx\n",
				(char) ((c->dbtype >> 24) & 0xff),
				(char) ((c->dbtype >> 16) & 0xff),
				(char) ((c->dbtype >> 8) & 0xff),
				(char) (c->dbtype & 0xff),
				c->dbtype);
			fprintf(stderr, "\tPath: [%s]\n", c->path);
		}
		
		fprintf(stderr, "Dump conduits:\n");
		for (c = config.dump_q; c != NULL; c = c->next)
		{
			fprintf(stderr, "  Conduit:\n");
			if (c->flavor != Dump)
				fprintf(stderr, "Error: wrong conduit flavor. "
					"Expected %d (Dump), but this is %d\n",
					Dump, c->flavor);
			fprintf(stderr, "\tCreator: [%c%c%c%c] 0x%08lx\n",
				(char) ((c->dbcreator >> 24) & 0xff),
				(char) ((c->dbcreator >> 16) & 0xff),
				(char) ((c->dbcreator >> 8) & 0xff),
				(char) (c->dbcreator & 0xff),
				c->dbcreator);
			fprintf(stderr, "\tType: [%c%c%c%c] 0x%08lx\n",
				(char) ((c->dbtype >> 24) & 0xff),
				(char) ((c->dbtype >> 16) & 0xff),
				(char) ((c->dbtype >> 8) & 0xff),
				(char) (c->dbtype & 0xff),
				c->dbtype);
			fprintf(stderr, "\tPath: [%s]\n", c->path);
		}
		
		fprintf(stderr, "Install conduits:\n");
		for (c = config.install_q; c != NULL; c = c->next)
		{
			fprintf(stderr, "  Conduit:\n");
			if (c->flavor != Install)
				fprintf(stderr, "Error: wrong conduit flavor. "
					"Expected %d (Install), but this is "
					"%d\n",
					Install, c->flavor);
			fprintf(stderr, "\tCreator: [%c%c%c%c] 0x%08lx\n",
				(char) ((c->dbcreator >> 24) & 0xff),
				(char) ((c->dbcreator >> 16) & 0xff),
				(char) ((c->dbcreator >> 8) & 0xff),
				(char) (c->dbcreator & 0xff),
				c->dbcreator);
			fprintf(stderr, "\tType: [%c%c%c%c] 0x%08lx\n",
				(char) ((c->dbtype >> 24) & 0xff),
				(char) ((c->dbtype >> 16) & 0xff),
				(char) ((c->dbtype >> 8) & 0xff),
				(char) (c->dbtype & 0xff),
				c->dbtype);
			fprintf(stderr, "\tPath: [%s]\n", c->path);
		}
		
		fprintf(stderr, "Uninstall conduits:\n");
		for (c = config.uninstall_q; c != NULL; c = c->next)
		{
			fprintf(stderr, "  Conduit:\n");
			if (c->flavor != Uninstall)
				fprintf(stderr, "Error: wrong conduit flavor. "
					"Expected %d (Uninstall), but this is "
					"%d\n",
					Uninstall, c->flavor);
			fprintf(stderr, "\tCreator: [%c%c%c%c] 0x%08lx\n",
				(char) ((c->dbcreator >> 24) & 0xff),
				(char) ((c->dbcreator >> 16) & 0xff),
				(char) ((c->dbcreator >> 8) & 0xff),
				(char) (c->dbcreator & 0xff),
				c->dbcreator);
			fprintf(stderr, "\tType: [%c%c%c%c] 0x%08lx\n",
				(char) ((c->dbtype >> 24) & 0xff),
				(char) ((c->dbtype >> 16) & 0xff),
				(char) ((c->dbtype >> 8) & 0xff),
				(char) (c->dbtype & 0xff),
				c->dbtype);
			fprintf(stderr, "\tPath: [%s]\n", c->path);
		}
	}

	if (user_config != NULL)
		free_config(user_config);

	return 0;
}

/* name2listen_type
 * Convert the name of a listen type to its integer value. See the LISTEN_*
 * defines in "coldsync.h".
 */
static int
name2listen_type(const char *str)
{
	/* XXX - It'd be really nice if these strings were translatable */
	if (strcasecmp(str, "serial") == 0)
		return LISTEN_SERIAL;
#if 0	/* XXX - Not implemented yet */
	if (strcasecmp(str, "tcp") == 0)
		return LISTEN_TCP;
#endif	/* 0 */
	if (strcasecmp(str, "usb") == 0)
		return LISTEN_USB;
	return -1;		/* None of the above */
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
	struct passwd *pwent;	/* /etc/passwd entry for current user */
	struct stat statbuf;	/* For checking for files and directories */
	pda_block *pda;		/* Description of the current PDA */

	/* Try to find a pda_block for the current PDA */
	{
		pda_block *cur;	/* The pda-block we're currently looking at */
		pda_block *default_pda;
				/* pda_block for the default PDA, if no
				 * better match is found.
				 */

		pda = NULL;
		default_pda = NULL;
		for (cur = config.pda; cur != NULL; cur = cur->next)
		{
			/* See if this pda_block has a serial number and if
			 * so, whether it matches the one we read off of
			 * the Palm.
			 * Palms with pre-3.0 ROMs don't have serial
			 * numbers. They can be represented in the
			 * .coldsyncrc file with
			 *	snum "";
			 * This does mean that you shouldn't have more than
			 * one pda_block with an empty string.
			 */
			if ((cur->snum != NULL) &&
			    (strncasecmp(cur->snum, palm->serial, SNUM_MAX)
			     != 0))
			{
				/* The serial number doesn't match */
				continue;
			}

			MISC_TRACE(3)
			{
				fprintf(stderr, "Found a match for "
					"this PDA:\n");
				fprintf(stderr, "\tS/N: [%s]\n",
					cur->snum);
				fprintf(stderr, "\tDirectory: [%s]\n",
					cur->directory);
			}

			if ((cur->flags & PDAFL_DEFAULT) != 0)
			{
				MISC_TRACE(3)
					fprintf(stderr,
						"Found a default PDA\n");

				/* Mark this as the default pda_block */
				default_pda = cur;
				continue;
			}

			/* If we get this far, then the serial number
			 * matches and this is not a default pda_block. So
			 * this is the one we want to use.
			 */
			pda = cur;
			break;
		}

		if (pda == NULL)
		{
			MISC_TRACE(3)
				fprintf(stderr, "No exact match found for "
					"this PDA. Using default\n");
			/* Fall back to the default pda_block */
			pda = default_pda;
		}
	}

	MISC_TRACE(3)
		if (pda == NULL)
			fprintf(stderr, "No PDA found in config file. Using "
				"system defaults.\n");

	/* Get the current user's UID */
	uid = getuid();		/* Can't fail, according to TFM */

	MISC_TRACE(2)
		fprintf(stderr, "UID: %lu\n", (unsigned long) uid);

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
		fprintf(stderr, "\tpw_uid: %lu\n",
			(unsigned long) pwent->pw_uid);
		fprintf(stderr, "\tpw_gecos: \"%s\"\n", pwent->pw_gecos);
		fprintf(stderr, "\tpw_dir: \"%s\"\n", pwent->pw_dir);
	}

	user_uid = pwent->pw_uid;	/* Get the user's UID */
 
	/* Make sure the various directories (~/.palm/...) exist, and create
	 * them if necessary.
	 */

	/* Figure out what the base directory is */
	if ((pda != NULL) && (pda->directory != NULL))
	{
		/* Use the directory specified in the config file */
		strncpy(palmdir, pda->directory, MAXPATHLEN);
	} else {
		/* Either there is no applicable PDA, or else it doesn't
		 * specify a directory. Use the default (~/.palm).
		 */
		strncpy(palmdir, userinfo.homedir, MAXPATHLEN);
		strncat(palmdir, "/.palm", MAXPATHLEN - strlen(palmdir));
	}
	MISC_TRACE(3)
		fprintf(stderr, "Base directory is [%s]\n", palmdir);

	/* ~/.palm */
	if (stat(palmdir, &statbuf) < 0)
	{
		/* ~/.palm doesn't exist. Create it */
		if ((err = mkdir(palmdir, DIR_MODE)) < 0)
		{
			/* Can't create the directory */
			perror("load_palm_config: mkdir(~/.palm)\n");
			return -1;
		}
	}

	/* ~/.palm/backup */
	strncpy(backupdir, palmdir, MAXPATHLEN);
	strncat(backupdir, "/backup", MAXPATHLEN - strlen(palmdir));

	if (stat(backupdir, &statbuf) < 0)
	{
		/* ~/.palm/backup doesn't exist. Create it */
		if ((err = mkdir(backupdir, DIR_MODE)) < 0)
		{
			/* Can't create the directory */
			perror("load_palm_config: mkdir(~/.palm/backup)\n");
			return -1;
		}
	}

	/* ~/.palm/backup/Attic */
	strncpy(atticdir, backupdir, MAXPATHLEN);
	strncat(atticdir, "/Attic", MAXPATHLEN - strlen(backupdir));

	if (stat(atticdir, &statbuf) < 0)
	{
		/* ~/.palm/backup/Attic doesn't exist. Create it */
		if ((err = mkdir(atticdir, DIR_MODE)) < 0)
		{
			/* Can't create the directory */
			perror("load_palm_config: "
			       "mkdir(~/.palm/backup/Attic)\n");
			return -1;
		}
	}

	/* ~/.palm/archive */
	strncpy(archivedir, palmdir, MAXPATHLEN);
	strncat(archivedir, "/archive", MAXPATHLEN - strlen(palmdir));

	if (stat(archivedir, &statbuf) < 0)
	{
		/* ~/.palm/archive doesn't exist. Create it */
		if ((err = mkdir(archivedir, DIR_MODE)) < 0)
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
		if ((err = mkdir(installdir, DIR_MODE)) < 0)
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
/* XXX - Is there a more specific test for egcs? */
#ifdef _GNUC_
#warning "You can ignore the warning about"
#warning "\"ANSI C forbids braced-groups within expressions\""
#endif	/* _GNUC_ */
			/* egcs whines about "ANSI C forbids braced-groups
			 * within expressions", but there doesn't seem to
			 * be anything I can do about it, since it's the
			 * __tobody macro inside <ctype.h> that's broken in
			 * this way.
			 */
			buf[bufi] = toupper((int) pwent->pw_name[0]);
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

/* new_config
 * Allocate a new configuration.
 */
struct config *
new_config()
{
	struct config *retval;

	retval = (struct config *) malloc(sizeof(struct config));
	if (retval == NULL)
		return NULL;		/* Out of memory */

	/* Initialize fields */
	retval->mode = Standalone;
	retval->listen		= NULL;
	retval->pda		= NULL;
	retval->sync_q		= NULL;
	retval->fetch_q		= NULL;
	retval->dump_q		= NULL;
	retval->install_q	= NULL;
	retval->uninstall_q	= NULL;

	MISC_TRACE(5)
		fprintf(stderr,
			"Allocated config 0x%08lx\n", (unsigned long) retval);
	return retval;
}

void
free_config(struct config *config)
{
	listen_block *l;
	listen_block *nextl;
	pda_block *p;
	pda_block *nextp;
	conduit_block *c;
	conduit_block *nextc;

	MISC_TRACE(5)
		fprintf(stderr, "Freeing config 0x%08lx\n",
			(unsigned long) config);

	/* Free the listen blocks */
	for (l = config->listen, nextl = NULL; l != NULL; l = nextl)
	{
		nextl = l->next;
		free_listen_block(l);
	}

	/* Free the PDA blocks */
	for (p = config->pda, nextp = NULL; p != NULL; p = nextp)
	{
		nextp = p->next;
		free_pda_block(p);
	}

	/* Free sync_q */
	for (c = config->sync_q, nextc = NULL; c != NULL; c = nextc)
	{
		nextc = c->next;
		free_conduit_block(c);
	}

	/* Free fetch_q */
	for (c = config->fetch_q, nextc = NULL; c != NULL; c = nextc)
	{
		nextc = c->next;
		free_conduit_block(c);
	}

	/* Free dump_q */
	for (c = config->dump_q, nextc = NULL; c != NULL; c = nextc)
	{
		nextc = c->next;
		free_conduit_block(c);
	}

	/* Free install_q */
	for (c = config->install_q, nextc = NULL; c != NULL; c = nextc)
	{
		nextc = c->next;
		free_conduit_block(c);
	}

	/* Free uninstall_q */
	for (c = config->uninstall_q, nextc = NULL; c != NULL; c = nextc)
	{
		nextc = c->next;
		free_conduit_block(c);
	}

	/* Free the config itself */
	free(config);
}

/* get_userinfo
 * Get information about the current user, as defined by the real UID.
 * Fill in the specified 'struct userinfo' structure.
 * XXX - In daemon mode, need to setuid() _before_ calling this.
 */
static int
get_userinfo(struct userinfo *userinfo)
{
	uid_t uid;		/* Current (real) uid */
	struct passwd *pwent;	/* Password entry for current user */
	char *home;		/* Value of $HOME */

	uid = getuid();
	userinfo->uid = uid;

	MISC_TRACE(2)
		fprintf(stderr, "UID: %lu\n", (unsigned long) uid);

	/* Get the user's password file info */
	if ((pwent = getpwuid(uid)) == NULL)
	{
		perror("get_userinfo: getpwuid");
		return -1;
	}

	/* Get the user's full name */
	if (get_fullname(userinfo->fullname, sizeof(userinfo->fullname),
			 pwent) < 0)
	{
		fprintf(stderr, _("Can't get user's full name.\n"));
		return -1;
	}

	/* Get the user's home directory */
	home = getenv("HOME");
	if (home == NULL)
	{
		/* There's no $HOME environment variable. Use the home
		 * directory from the password file entry.
		 */
		home = pwent->pw_dir;
	}
	strncpy(userinfo->homedir, home,
		sizeof(userinfo->homedir) - 1);
	userinfo->homedir[sizeof(userinfo->homedir)-1] = '\0';
				/* Make sure the string is terminated */

	MISC_TRACE(2)
		fprintf(stderr, "HOME: \"%s\"\n", userinfo->homedir);

	return 0;
}

/* new_listen_block
 * Allocate and initialize a new listen_block.
 * Returns a pointer to the new listen_block, or NULL in case of error.
 */
listen_block *
new_listen_block()
{
	listen_block *retval;

	/* Allocate the new listen_block */
	if ((retval = (listen_block *) malloc(sizeof(listen_block))) == NULL)
		return NULL;

	/* Initialize the new listen_block */
	retval->next = NULL;
	retval->listen_type = LISTEN_SERIAL;	/* By default */
	retval->device = NULL;
	retval->speed = 0;

	return retval;
}

/* free_listen_block
 * Free a listen block. Note that this function does not pay attention to
 * any other listen_blocks on the list. If you have a list of
 * listen_blocks, you can use this function to free each individual element
 * in the list, but not the whole list.
 */
void
free_listen_block(listen_block *l)
{
	if (l->device != NULL)
		free(l->device);
	free(l);
}

/* new_conduit_block
 * Allocate and initialize a new conduit_block.
 */
conduit_block *
new_conduit_block()
{
	conduit_block *retval;

	/* Allocate the new conduit_block */
	if ((retval = (conduit_block *) malloc(sizeof(conduit_block))) == NULL)
		return NULL;

	/* Initialize the new conduit_block */
	retval->next = NULL;
	retval->flavor = Sync;
	retval->dbtype = 0L;
	retval->dbcreator = 0L;
	retval->flags = 0;
	retval->path = NULL;

	return retval;
}

/* free_conduit_block
 * Free a conduit block. Note that this function does not pay attention to
 * any other conduit_blocks on the list. If you have a list of
 * conduit_blocks, you can use this function to free each individual element
 * in the list, but not the whole list.
 */
void
free_conduit_block(conduit_block *c)
{
	if (c->path != NULL)
		free(c->path);
	free(c);
}

/* new_pda_block
 * Allocate and initialize a new pda_block.
 * Returns a pointer to the new pda_block, or NULL in case of error.
 */
pda_block *
new_pda_block()
{
	pda_block *retval;

	/* Allocate the new pda_block */
	if ((retval = (pda_block *) malloc(sizeof(pda_block))) == NULL)
		return NULL;

	/* Initialize the new pda_block */
	retval->next = NULL;
	retval->snum = NULL;
	retval->directory = NULL;

	return retval;
}

/* free_pda_block
 * Free a pda block. Note that this function does not pay attention to any
 * other pda_blocks on the list. If you have a list of pda_blocks, you can
 * use this function to free each individual element in the list, but not
 * the whole list.
 */
void
free_pda_block(pda_block *p)
{
	if (p->snum != NULL)
		free(p->snum);
	if (p->directory != NULL)
		free(p->directory);
	free(p);
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */