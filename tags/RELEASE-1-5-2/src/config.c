/* config.c
 *
 * Functions dealing with loading the configuration.
 *
 *	Copyright (C) 1999, 2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: config.c,v 1.42 2000-11-19 00:11:22 arensb Exp $
 */
#include "config.h"
#include <stdio.h>
#include <unistd.h>		/* For getuid(), gethostname() */
#include <stdlib.h>		/* For atoi(), getenv() */
#include <sys/types.h>		/* For getuid(), getpwuid() */
#include <sys/stat.h>		/* For mkdir() */
#include <pwd.h>		/* For getpwuid() */
#include <sys/param.h>		/* For MAXPATHLEN */
#include <netdb.h>		/* For gethostbyname() */
#include <sys/socket.h>		/* For AF_INET */
#include <string.h>		/* For string functions */
#include <ctype.h>		/* For toupper() */

#if HAVE_LIBINTL_H
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL_H */

#include "coldsync.h"
#include "pconn/pconn.h"
#include "parser.h"		/* For config file parser stuff */

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN	256
#endif	/* MAXHOSTNAMELEN */

#define PALMDEV		"/dev/palm"	/* Default device */
#define DIR_MODE	0700		/* Default permissions for new
					 * directories. */

/* Declarations of everything related to getopt(), for those OSes that
 * don't have it already (Windows NT). Note that not all of these are used.
 */
extern int getopt(int argc, char * const *argv, const char *optstring);
extern char *optarg;
extern int optind;
extern int optopt;
extern int opterr;
extern int optreset;

extern struct config config;

/* For debugging only */
extern void debug_dump(FILE *outfile, const char *prefix,
		       const ubyte *buf, const udword len);

/* XXX - This should probably be hidden inside a "struct config{..}" or
 * something. I don't like global variables.
 */
udword hostid;			/* This machine's host ID, so you can tell
				 * whether this was the last machine the
				 * Palm synced with.
				 */
char palmdir[MAXPATHLEN+1];	/* ~/.palm pathname */
char backupdir[MAXPATHLEN+1];	/* ~/.palm/backup pathname */
char atticdir[MAXPATHLEN+1];	/* ~/.palm/backup/Attic pathname */
char archivedir[MAXPATHLEN+1];	/* ~/.palm/archive pathname */
char installdir[MAXPATHLEN+1];	/* ~/.palm/install pathname */

char conf_fname[MAXPATHLEN+1];	/* ~/.coldsyncrc */

struct userinfo userinfo;	/* Information about the Palm's owner */
	/* XXX - Probably should go in sync_config */

static int name2listen_type(const char *str);
static int get_fullname(char *buf, const int buflen,
			const struct passwd *pwent);
static int get_userinfo(struct userinfo *userinfo);

/* parse_args
 * Parse command-line arguments.
 * Returns -1 on error. Returns 0 if the program should stop now ("-h" and
 * "-V" options). If successful, returns the number of arguments parsed.
 */
int
parse_args(int argc, char *argv[])
{
	int err;
	int oldoptind;			/* Previous value of 'optind', to
					 * allow us to figure out exactly
					 * which argument was bogus, and
					 * thereby print descriptive error
					 * messages. */
	int arg;			/* Current option */

	oldoptind = optind;		/* Initialize "last argument"
					 * index.
					 */

	/* Read command-line options. */
	while ((arg = getopt(argc, argv, ":hVSFRIf:m:b:r:p:t:d:")) != -1)
		/* XXX - The "-b" and "-r" options are obsolete, and should
		 * be removed some time after v1.4.6.
		 */
	{
		switch (arg)
		{
		    case 'h':	/* -h: Print usage message and exit */
			usage(argc, argv);
			return 0;

		    case 'V':	/* -V: Print version number and exit */
			print_version();
			return 0;

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
			global_opts.conf_fname = optarg;
			global_opts.conf_fname_given = True;
			break;

		    case 'm':	/* -m <mode>: Run in the given mode */
			err = set_mode(optarg);
			if (err < 0)
			{
				usage(argc, argv);
				return -1;
			}
			break;

		    case 'b':	/* -b <dir>: Do a full backup to <dir> */
			/* XXX - This option is obsolete. Remove it some
			 * time after v1.4.6.
			 */
			fprintf(stderr,
				_("Warning: The \"-b <dir>\" option is "
				  "obsolete. Please use \"-mb <dir>\"\n"
				  "instead.\n"));
			global_opts.mode = mode_Backup;
			global_opts.do_backup = True;
			global_opts.backupdir = optarg;
			break;

		    case 'r':	/* -r <dir>: Do a restore from <dir> */
			/* XXX - This option is obsolete. Remove it some
			 * time after v1.4.6.
			 */
			fprintf(stderr,
				_("Warning: The \"-r <dir>\" option is "
				  "obsolete. Please use \"-mr <dir>\"\n"
				  "instead.\n"));
			global_opts.mode = mode_Restore;
			global_opts.do_restore = True;
			global_opts.restoredir = optarg;
			break;

		    case 'p':	/* -p <device>: Listen on serial port
				 * <device>
				 */
			global_opts.devname = optarg;
			break;

		    case 't':	/* -t <device type>: Listen on port device
				 * of type <device type>
				 */
			global_opts.devtype = name2listen_type(optarg);
			if (global_opts.devtype < 0)
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

		oldoptind = optind;	/* Update for next iteration */
	}

	return optind;
}

/* load_config
 * Load the global configuration file /etc/coldsync.conf. If we're
 * not running in daemon mode, load the user's .coldsyncrc as well.
 */
int
load_config()
{
	int err;

	/* Allocate a place to put the user's configuration */
	if ((sync_config = new_sync_config()) == NULL)
	{
		fprintf(stderr, _("Can't allocate new configuration.\n"));
		return -1;
	}

	/* Add a default conduit to the head of the queue, equivalent
	 * to:
	 * 	conduit sync {
	 *		type: * / *;
	 *		path: [generic];
	 *	}
	 */
	{
		conduit_block *fallback;	/* The generic default
						 * conduit */

		/* Allocate a new conduit block */
		if ((fallback = new_conduit_block()) == NULL)
		{
			fprintf(stderr,
				_("Can't allocate new conduit block.\n"));
			free_sync_config(sync_config);
			sync_config = NULL;
			return -1;
		}

		/* Initialize the fallback conduit. */
		fallback->flavors = FLAVORFL_SYNC;	/* Sync flavor */
		append_crea_type(fallback, 0x0000, 0x0000);
					/* Handles all creators and types */
		fallback->flags |= CONDFL_DEFAULT;
					/* This is a default conduit */
		fallback->path = strdup("[generic]");
		if (fallback->path == NULL)
		{
			/* strdup() failed */
			fprintf(stderr,
				_("Error initializing configuration.\n"));
			perror("strdup");

			free_conduit_block(fallback);
			free_sync_config(sync_config);
			sync_config = NULL;
			return -1;
		}

		/* Append this conduit to the (empty) conduit queue */
		sync_config->conduits = fallback;
	}

	/* Read /etc/coldsync.conf */
	if (exists(DEFAULT_GLOBAL_CONFIG))
	{
		MISC_TRACE(3)
			fprintf(stderr, "Reading \"%s\"\n",
				DEFAULT_GLOBAL_CONFIG);

		err = parse_config_file(DEFAULT_GLOBAL_CONFIG, sync_config);
		if (err < 0)
		{
			fprintf(stderr,
				_("Error reading configuration file \"%s\"\n"),
				DEFAULT_GLOBAL_CONFIG);
			free_sync_config(sync_config);
			sync_config = NULL;
			return -1;
		}
	}

	/* Read ~/.coldsyncrc */
	if (global_opts.mode != mode_Daemon)
	{
		struct userinfo userinfo;	/* User's /etc/passwd entry */

		if (global_opts.conf_fname_given &&
		    !exists(global_opts.conf_fname))
		{
			/* A config file was specified on the command line,
			 * but it doesn't exist. Warn and continue with the
			 * defaults.
			 */
			fprintf(stderr,
				_("Error: config file \"%s\" doesn't "
				  "exist.\n"),
				global_opts.conf_fname);

			return -1;
		}

		if (global_opts.conf_fname == NULL)
		{
			err = get_userinfo(&userinfo);
			if (err < 0)
			{
				fprintf(stderr, _("Can't get user info\n"));
				return -1;
			}

			/* Construct the full pathname to ~/.coldsyncrc in
			 * 'conf_fname'.
			 */
			strncpy(conf_fname, userinfo.homedir, MAXPATHLEN);
			strncat(conf_fname, "/.coldsyncrc",
				MAXPATHLEN - strlen(conf_fname));
			global_opts.conf_fname = conf_fname;
		}

		MISC_TRACE(3)
			fprintf(stderr, "Reading \"%s\"\n",
				global_opts.conf_fname);

		err = parse_config_file(global_opts.conf_fname, sync_config);
		if (err < 0)
		{
			fprintf(stderr,
				_("Error reading configuration file \"%s\"\n"),
				global_opts.conf_fname);
			free_sync_config(sync_config);
			sync_config = NULL;
			return -1;
		}
	}

	/* Make sure there's at least one listen block: if a port was
	 * specified on the command line, use that. If nothing was
	 * specified on the command line or in the config files, create a
	 * default listen block.
	 */
	/* XXX - This is a hack */
	if (global_opts.devname != NULL)
	{
		/* A device was specified on the command line */
		listen_block *l;

		MISC_TRACE(4)
			fprintf(stderr,
				"Device specified on command line: \"%s\"\n", 
				global_opts.devname);

		if ((l = new_listen_block()) == NULL)
		{
			fprintf(stderr, _("Can't allocate listen block.\n"));
			free_sync_config(sync_config);
			sync_config = NULL;
			return -1;
		}

		if ((l->device = strdup(global_opts.devname)) == NULL)
		{
			fprintf(stderr, _("Can't copy string.\n"));
			free_listen_block(l);
			free_sync_config(sync_config);
			sync_config = NULL;
			return -1;
		}

		if (global_opts.devtype >= 0)
			l->listen_type = global_opts.devtype;

		/* Prepend the new listen block to the list of listen blocks */
		l->next = sync_config->listen;
		sync_config->listen = l;
		l = NULL;
	}

	if (sync_config->listen == NULL)
	{
		/* No device specified either on the command line or in the
		 * config files. Create a new, default listen block.
		 */
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
			free_sync_config(sync_config);
			sync_config = NULL;
			return -1;
		}

		if ((l->device = strdup(PALMDEV)) == NULL)
		{
			fprintf(stderr, _("Can't copy string.\n"));
			free_listen_block(l);
			free_sync_config(sync_config);
			sync_config = NULL;
			return -1;
		}

		/* Make the new listen block be the one for the main
		 * configuration.
		 */
		sync_config->listen = l;
		l = NULL;
	}

	SYNC_TRACE(4)
	{
		/* Dump a summary of the config file */
		listen_block *l;
		pda_block *p;
		conduit_block *c;

		fprintf(stderr, "Summary of sync configuration:\n");
		for (l = sync_config->listen; l != NULL; l = l->next)
		{
			fprintf(stderr, "Listen:\n");
			fprintf(stderr, "\tType: %d\n", l->listen_type);
			fprintf(stderr, "\tDevice: [%s]\n", l->device);
			fprintf(stderr, "\tSpeed: %ld\n", l->speed);
		}

		fprintf(stderr, "Known PDAs:\n");
		for (p = sync_config->pda; p != NULL; p = p->next)
		{
			fprintf(stderr, "PDA:\n");
			fprintf(stderr, "\tSerial number: [%s]\n",
				(p->snum == NULL ? "(null)" : p->snum));
			fprintf(stderr, "\tDirectory: [%s]\n",
				(p->directory == NULL ? "(null)" :
				 p->directory));
			fprintf(stderr, "\tUsername: [%s]\n",
				(p->username == NULL ? "(null)" :
				 p->username));
			fprintf(stderr, "\tUserID: %ld\n",
				p->userid);
			fprintf(stderr, "\tFlags:");
			if ((p->flags & PDAFL_DEFAULT) != 0)
				fprintf(stderr, " DEFAULT");
			fprintf(stderr, "\n");
		}

		fprintf(stderr, "The queue of conduits:\n");
		for (c = sync_config->conduits; c != NULL; c = c->next)
		{
			struct cond_header *hdr;
			int i;

			fprintf(stderr, "  Conduit:\n");
			fprintf(stderr, "\tflavors: 0x%04x", c->flavors);
			if (c->flavors & FLAVORFL_FETCH)
				fprintf(stderr, " FETCH");
			if (c->flavors & FLAVORFL_DUMP)
				fprintf(stderr, " DUMP");
			if (c->flavors & FLAVORFL_SYNC)
				fprintf(stderr, " SYNC");
			if (c->flavors & FLAVORFL_INSTALL)
				fprintf(stderr, " INSTALL");
			fprintf(stderr, "\n");
			fprintf(stderr, "\tCreator/Types:\n");
			for (i = 0; i < c->num_ctypes; i++)
			{
				register udword crea = c->ctypes[i].creator;
				register udword type = c->ctypes[i].type;

				fprintf(stderr,
					"\t  [%c%c%c%c/%c%c%c%c] "
					"(0x%08lx/0x%08lx)\n",
					(char) ((crea >> 24) & 0xff),
					(char) ((crea >> 16) & 0xff),
					(char) ((crea >> 8) & 0xff),
					(char) (crea & 0xff),
					(char) ((type >> 24) & 0xff),
					(char) ((type >> 16) & 0xff),
					(char) ((type >> 8) & 0xff),
					(char) (type & 0xff),
					crea,
					type);
			}
			fprintf(stderr, "\tPath: [%s]\n", c->path);
			if ((c->flags & CONDFL_DEFAULT) != 0)
				fprintf(stderr, "\tDEFAULT\n");
			if ((c->flags & CONDFL_FINAL) != 0)
				fprintf(stderr, "\tFINAL\n");
			fprintf(stderr, "\tHeaders:\n");
			for (hdr = c->headers; hdr != NULL; hdr = hdr->next)
			{
				fprintf(stderr, "\t  [%s]: [%s]\n",
					hdr->name, hdr->value);
			}
			fprintf(stderr, "\tPreferences:\n");
			for (i = 0; i < c->num_prefs; i++)
			{
				fprintf(stderr,
					"\t  [%c%c%c%c] 0x%08lx / %d",
					(char) ((c->prefs[i].creator >> 24) &
						0xff),
					(char) ((c->prefs[i].creator >> 16) &
						0xff),
					(char) ((c->prefs[i].creator >> 8) &
						0xff),
					(char) (c->prefs[i].creator & 0xff),
					c->prefs[i].creator,
					c->prefs[i].id);
				if ((c->prefs[i].flags & PREFDFL_SAVED) != 0)
					fprintf(stderr, " SAVED");
				if ((c->prefs[i].flags & PREFDFL_UNSAVED) != 0)
					fprintf(stderr, " UNSAVED");
				fprintf(stderr, "\n");
			}
		}
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
	if (strncasecmp(str, "slp", 3) == 0)
		slp_trace = lvl;
	else if (strncasecmp(str, "cmp", 3) == 0)
		cmp_trace = lvl;
	else if (strncasecmp(str, "padp", 4) == 0)
		padp_trace = lvl;
	else if (strncasecmp(str, "dlpc", 4) == 0)
		dlpc_trace = lvl;
	else if (strncasecmp(str, "dlp", 3) == 0)
		dlp_trace = lvl;
	else if (strncasecmp(str, "sync", 4) == 0)
		sync_trace = lvl;
	else if (strncasecmp(str, "pdb", 3) == 0)
		pdb_trace = lvl;
	else if (strncasecmp(str, "parse", 5) == 0)
		parse_trace = lvl;
	else if (strncasecmp(str, "misc", 4) == 0)
		misc_trace = lvl;
	else if (strncasecmp(str, "io", 2) == 0)
		io_trace = lvl;
	else {
		fprintf(stderr, _("Unknown facility \"%s\"\n"), str);
	}
}

/* set_mode
 * Given a mode string, set the mode indicated by the string. Returns 0 if
 * successful, or a negative value in case of error. Currently, the only
 * error is "unknown mode".
 */
int
set_mode(const char *str)
{
	if (str == NULL)	/* Sanity check */
	{
		/* This should never happen */
		fprintf(stderr, _("%s: Missing mode string. "
				  "This should never happen.\n"),
			"set_mode");
		return -1;
	}

	/* Sanity check: if the user specifies the mode twice, complain. */
	if (global_opts.mode != mode_None)
		fprintf(stderr,
			_("Warning: You shouldn't specify two -m<mode> "
			  "options.\n"
			  "\tUsing -m%s\n"),
			str);

	switch (str[0])
	{
	    case 's':		/* Standalone mode. Default */
		global_opts.mode = mode_Standalone;
		return 0;

	    case 'b':		/* Backup mode */
		global_opts.mode = mode_Backup;
		return 0;

	    case 'r':		/* Restore mode */
		global_opts.mode = mode_Restore;
		return 0;

#if 0	/* Not implemented yet */
	    case 'i':		/* Install mode */
		global_opts.mode = mode_Install;
		return 0;

	    case 'd':		/* Daemon mode */
		global_opts.mode = mode_Daemon;
		return 0;

	    case 'g':		/* Getty mode */
		global_opts.mode = mode_Getty;
		return 0;

	    case 'I':		/* Init mode */
		global_opts.mode = mode_Init;
		return 0;
#endif	/* 0 */

	    default:
		fprintf(stderr, _("Error: unknown mode: \"%s\"\n"), str);
		return -1;
	}

	return -1;
}

/* usage
 * Print out a usage string.
 */
/* ARGSUSED */
void
usage(int argc, char *argv[])
{
	/* XXX - Very much out of date. Rewrite this. */
	printf(_("Usage: %s [options] -p port\n"
		 "Options:\n"
		 "\t-h:\t\tPrint this help message and exit.\n"
		 "\t-V:\t\tPrint version and exit.\n"
		 "\t-f <file>:\tRead configuration from <file>.\n"
		 "\t-b <dir>:\tPerform a backup to <dir>.\n"
		 "\t-r <dir>:\tRestore from <dir>.\n"
		 "\t-I:\t\tForce installation of new databases.\n"
		 "\t-S:\t\tForce slow sync.\n"
		 "\t-F:\t\tForce fast sync.\n"
		 "\t-R:\t\tCheck ROM databases.\n"
		 "\t-p <port>:\tListen on device <port>\n"
		 "\t-t <devtype>:\tPort type [serial|usb]\n"
		 "\t-d <fac[:level]>:\tSet debugging level.\n")
	       ,
	       argv[0]);
}

/* print_version
 * Print out the version of ColdSync.
 */
void
print_version(void)
{
	printf(_("%s version %s\n"),
	       /* These two strings are defined in "config.h" */
	       PACKAGE,
	       VERSION);
	printf(_("ColdSync homepage at http://www.ooblick.com/software/"
		 "coldsync/\n"));
	/* XXX - Ought to print out other information, e.g., copyright,
	 * compile-time flags, optional packages, maybe OS name and
	 * version, who compiled it and when, etc.
	 */
	printf(_("Compile-type options:\n"));

#if WITH_USB
	printf(
_("    WITH_USB: USB support.\n"));
#endif	/* WITH_USB */
#if WITH_EFENCE
	printf(
_("    WITH_EFENCE: buffer overruns will cause a segmentation violation.\n"));
#endif	/* WITH_EFENCE */

#if HAVE_STRCASECMP && HAVE_STRNCASECMP
	printf(
_("    HAVE_STRCASECMP, HAVE_STRNCASECMP: strings are compared without regard\n"
"        to case, whenever possible.\n"));
#endif	/* HAVE_STRCASECMP && HAVE_STRNCASECMP */
/* XXX */
	printf(
_("\n"
"    Default global configuration file: %s\n"),
		DEFAULT_GLOBAL_CONFIG);
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
		for (cur = sync_config->pda; cur != NULL; cur = cur->next)
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
			    (strncasecmp(cur->snum, palm_serial(palm),
					 SNUM_MAX)
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

	/* See if the PDA block has overridden the default user name.
	 */
	if ((pda != NULL) && (pda->username != NULL))
	{
		MISC_TRACE(4)
			fprintf(stderr, "Overriding user name: [%s]\n",
				pda->username);
		strncpy(userinfo.fullname, pda->username,
			sizeof(userinfo.fullname));
	}

	/* See if the PDA block has overridden the user ID */
	if ((pda != NULL) && (pda->userid != 0))
	{
		/* Use the UID supplied by the PDA block */
		MISC_TRACE(4)
			fprintf(stderr, "Overriding user ID: %ld\n",
				pda->userid);
		userinfo.uid = pda->userid;
	} else {
		/* Use the current user's UID */
		userinfo.uid = pwent->pw_uid;	/* Get the user's UID */
	}
 
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
	if (!is_directory(palmdir))
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

	if (!is_directory(backupdir))
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

	if (!is_directory(atticdir))
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

	if (!is_directory(archivedir))
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

	if (!is_directory(installdir))
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

/* new_sync_config
 * Allocate a new sync configuration.
 */
struct sync_config *
new_sync_config()
{
	struct sync_config *retval;

	retval = (struct sync_config *) malloc(sizeof(struct sync_config));
	if (retval == NULL)
		return NULL;		/* Out of memory */

	/* Initialize fields */
	retval->listen		= NULL;
	retval->pda		= NULL;
	retval->conduits	= NULL;

	MISC_TRACE(5)
		fprintf(stderr,
			"Allocated sync_config 0x%08lx\n",
			(unsigned long) retval);
	return retval;
}

/* free_sync_config
 * Free a sync configuration.
 */
void
free_sync_config(struct sync_config *config)
{
	listen_block *l;
	listen_block *nextl;
	pda_block *p;
	pda_block *nextp;
	conduit_block *c;
	conduit_block *nextc;

	MISC_TRACE(5)
		fprintf(stderr, "Freeing sync_config 0x%08lx\n",
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

	/* Free conduits */
	for (c = config->conduits, nextc = NULL; c != NULL; c = nextc)
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
	retval->speed = 0L;

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
	retval->flavors = 0;
	retval->ctypes = NULL;
	retval->ctypes_slots = 0;
	retval->num_ctypes = 0;
	retval->flags = 0;
	retval->path = NULL;
	retval->headers = NULL;
	retval->prefs = NULL;
	retval->prefs_slots = 0;
	retval->num_prefs = 0;

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
	struct cond_header *hdr;
	struct cond_header *next_hdr;

	if (c->path != NULL)
		free(c->path);

	/* Free the conduit headers */
	for (hdr = c->headers, next_hdr = NULL; hdr != NULL; hdr = next_hdr)
	{
		next_hdr = hdr->next;
		free(hdr);
	}

	/* Free the 'ctypes' array */
	if (c->ctypes != NULL)
		free(c->ctypes);

	/* Free the 'pref_desc' array */
	if (c->prefs != NULL)
		free(c->prefs);

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
	retval->name = NULL;
	retval->snum = NULL;
	retval->directory = NULL;
	retval->username = NULL;
	retval->userid = 0L;

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
	if (p->name != NULL)
		free(p->name);
	if (p->snum != NULL)
		free(p->snum);
	if (p->directory != NULL)
		free(p->directory);
	if (p->username != NULL)
		free(p->username);
	free(p);
}

/* append_pref_desc
 * Appends a preference descriptor to the conduit_block 'cond'.
 * Returns 0 if successful, a negative value otherwise.
 */
int
append_pref_desc(conduit_block *cond,	/* Conduit block to add to */
		 const udword creator,	/* Preference creator */
		 const uword id,	/* Preference identifier */
		 const char flags)	/* Preference flags (see PREFDFL_*) */
{
	/* Is this the first preference being added? */
	if (cond->prefs == NULL)
	{
		/* Yes. Start by allocating size for 4 entries, for
		 * starters.
		 */
		MISC_TRACE(7)
			fprintf(stderr, "Allocating a new 'prefs' array.\n");
		if ((cond->prefs = (struct pref_desc *)
		     calloc(4, sizeof(struct pref_desc))) == NULL)
		{
			/* Can't allocate new array */
			return -1;
		}
		cond->prefs_slots = 4;

	} else if (cond->num_prefs >= cond->prefs_slots)
	{
		struct pref_desc *newprefs;

		/* This is not the first preference, but the 'prefs' array
		 * is full and needs to be extended. Double its length.
		 */
		MISC_TRACE(7)
			fprintf(stderr, "Extending prefs array to %d\n",
				cond->prefs_slots * 2);
		if ((newprefs = (struct pref_desc *)
		     realloc(cond->prefs, 2 * cond->prefs_slots *
			     sizeof(struct pref_desc))) == NULL)
		{
			/* Can't extend array */
			return -1;
		}
		cond->prefs = newprefs;
		cond->prefs_slots *= 2;
	}

	/* If we get this far, then cond->prefs is long enough to hold the
	 * new preference descriptor. Add it.
	 */
	cond->prefs[cond->num_prefs].creator = creator;
	cond->prefs[cond->num_prefs].id = id;
	cond->prefs[cond->num_prefs].flags = flags;
	cond->num_prefs++;

	return 0;		/* Success */
}

/* append_crea_type
 * Appends a creator/type pair to the conduit_block 'cond'.
 * Returns 0 if successful, a negative value otherwise.
 */
int
append_crea_type(conduit_block *cond,	/* Conduit block to add to */
		 const udword creator,	/* Database creator */
		 const udword type)	/* Database type */
{
	/* Is this the first creator/type pair being added? */
	if (cond->ctypes == NULL)
	{
		/* Yes. Start by allocating size for 1 entry, for
		 * starters.
		 * (In most memory-allocation schemes of this type, one
		 * allocates room for more than one element, but in this
		 * case, the vast majority of conduits will only have one
		 * element.)
		 */
		MISC_TRACE(7)
			fprintf(stderr, "Allocating a new 'ctypes' array.\n");
		if ((cond->ctypes = (crea_type_t *)
		     calloc(4, sizeof(crea_type_t))) == NULL)
		{
			/* Can't allocate new array */
			return -1;
		}
		cond->ctypes_slots = 4;

	} else if (cond->num_ctypes >= cond->ctypes_slots)
	{
		crea_type_t *newctypes;

		/* This is not the first creator/type pair, but the
		 * 'ctypes' array is full and needs to be extended. Double
		 * its length.
		 */
		MISC_TRACE(7)
			fprintf(stderr, "Extending ctypes array to %d\n",
				cond->ctypes_slots * 2);
		if ((newctypes = (crea_type_t *)
		     realloc(cond->ctypes, 2 * cond->ctypes_slots *
			     sizeof(crea_type_t))) == NULL)
		{
			/* Can't extend array */
			return -1;
		}
		cond->ctypes = newctypes;
		cond->ctypes_slots *= 2;
	}

	/* If we get this far, then cond->ctypes is long enough to hold the
	 * new creator/type pair. Add it.
	 */
	cond->ctypes[cond->num_ctypes].creator = creator;
	cond->ctypes[cond->num_ctypes].type = type;
	cond->num_ctypes++;

	return 0;		/* Success */
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
