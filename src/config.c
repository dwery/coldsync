/* config.c
 *
 * Functions dealing with loading the configuration.
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: config.c,v 1.72.2.2 2001-10-11 04:26:13 arensb Exp $
 */
#include "config.h"
#include <stdio.h>
#include <unistd.h>		/* For getuid(), gethostname() */
#include <stdlib.h>		/* For atoi() */
#include <sys/types.h>		/* For getuid(), getpwuid() */
#include <sys/stat.h>		/* For mkdir() */
#include <sys/ioctl.h>		/* For ioctl() and ioctl values */
#include <pwd.h>		/* For getpwuid() */
#include <sys/param.h>		/* For MAXPATHLEN */
#include <netdb.h>		/* For gethostbyname2() */
#include <sys/socket.h>		/* For socket() */

#if HAVE_STROPTS_H
#  include <stropts.h>		/* For ioctl() under DU */
#endif	/* HAVE_STROPTS_H */

#if HAVE_SYS_SOCKIO_H
#  include <sys/sockio.h>	/* For SIOCGIFCONF under Solaris */
#endif	/* HAVE_SYS_SOCKIO_H */

/* DU's headers appear to be broken: <net/if.h> refers to these structures
 * before they're defined.
 */
struct mbuf;
struct ifaddr;
struct ifmulti;
struct rtentry;

#include <net/if.h>		/* For struct ifreq */
#include <netinet/in.h>		/* For struct sockaddr_in */
#include <arpa/inet.h>		/* For inet_ntop() and friends */
#include <string.h>		/* For string functions */
#include <ctype.h>		/* For toupper() */
#include <errno.h>		/* For errno(). Duh. */

#if HAVE_LIBINTL_H
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL_H */

#include "coldsync.h"
#include "pconn/pconn.h"
#include "parser.h"		/* For config file parser stuff */
#include "symboltable.h"
#include "cs_error.h"

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
struct sockaddr **hostaddrs = NULL;
				/* NULL-terminated array of addresses local
				 * to this host */
int num_hostaddrs = 0;		/* Number of addresses in 'hostaddrs' */

char hostname[MAXHOSTNAMELEN+1];
				/* This machine's hostname */
char palmdir[MAXPATHLEN+1];	/* ~/.palm pathname */
char backupdir[MAXPATHLEN+1];	/* ~/.palm/backup pathname */
char atticdir[MAXPATHLEN+1];	/* ~/.palm/backup/Attic pathname */
char archivedir[MAXPATHLEN+1];	/* ~/.palm/archive pathname */
char installdir[MAXPATHLEN+1];	/* ~/.palm/install pathname */

char conf_fname[MAXPATHLEN+1];	/* ~/.coldsyncrc */

struct userinfo userinfo;	/* Information about the Palm's owner */
	/* XXX - Probably should go in sync_config */

static void set_debug_level(const char *str);
static int set_mode(const char *str);
static void usage(int argc, char *argv[]);
static int name2listen_type(const char *str);
static int name2protocol(const char *str);
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

	/* XXX - Set mode based on argv[0]:
	 * "coldbackup"		-> mode_Backup
	 * "coldrestore"	-> mode_Restore
	 * "coldinstall"	-> mode_Restore
	 */

	/* Read command-line options. */
	opterr = 0;			/* Don't want getopt() writing to
					 * stderr */
	while ((arg = getopt(argc, argv, ":hvVSFRIszf:l:m:b:r:p:t:P:d:"))
	       != -1)
		/* XXX - The "-b" and "-r" options are obsolete, and should
		 * be removed some time after v1.4.6.
		 * How about 2.0.0? Sounds like a good breakpoint.
		 */
	{
		switch (arg)
		{
		    case 'h':	/* -h: Print usage message and exit */
			usage(argc, argv);
			return 0;

		    case 'v':	/* -v: Increase verbosity */
			global_opts.verbosity++;
			break;

		    case 'V':	/* -V: Print version number and exit */
			print_version(stdout);
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

		    case 'z':	/* -z: Install databases after main sync */
			global_opts.install_first = False;
			break;

		    case 'f':	/* -f <file>: Read configuration from
				 * <file>.
				 */
			global_opts.conf_fname = optarg;
			global_opts.conf_fname_given = True;
			break;

		    case 's':	/* -s: Use syslog for error messages */
			global_opts.use_syslog = True;
			break;

		    case 'l':	/* -l <file>: Write debugging log to
				 * <file>.
				 */
 			put_symbol("LOGFILE", optarg);
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
			Warn(_("The \"-b <dir>\" option is obsolete. "
			       "Please use \"-mb <dir>\"\n"
			       "instead."));
			global_opts.mode = mode_Backup;
			global_opts.do_backup = True;
			global_opts.backupdir = optarg;
			break;

		    case 'r':	/* -r <dir>: Do a restore from <dir> */
			/* XXX - This option is obsolete. Remove it some
			 * time after v1.4.6.
			 */
			Warn(_("The \"-r <dir>\" option is obsolete. "
			       "Please use \"-mr <dir>\"\n"
			       "instead."));
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
				Error(_("Unknown device type: \"%s\"."),
				      optarg);
				usage(argc, argv);
				return -1;
			}
			break;

		    case 'P':	/* -P <protocol>: Use the named software
				 * for talking to the cradle.
				 */
			global_opts.protocol = name2protocol(optarg);
			if (global_opts.protocol < 0)
			{
				Error(_("Unknown protocol: \"%s\"."),
				      optarg);
				usage(argc, argv);
				return -1;
			}
			break;

		    case 'd':	/* -d <fac>:<n>: Debugging level */
			set_debug_level(optarg);
			break;

		    case '?':	/* Unknown option */
			Error(_("Unrecognized option: \"%s\"."),
				argv[oldoptind]);
			usage(argc, argv);
			return -1;

		    case ':':	/* An argument required an option, but none
				 * was given (e.g., "-u" instead of "-u
				 * daemon").
				 */
			Error(_("Missing option argument after \"%s\"."),
			      argv[oldoptind]);
			usage(argc, argv);
			return -1;

		    default:
			Warn(_("You specified an apparently legal option "
			       "(\"-%c\"), but I don't know what\n"
			       "to do with it. This is a bug. Please "
			       "notify the maintainer."),
			     arg);
			return -1;
			break;
		}

		oldoptind = optind;	/* Update for next iteration */
	}

	return optind;
}

/* load_config
 * Load the global configuration file /etc/coldsync.conf.
 * If 'read_user_config' is true, load the user's .coldsyncrc as well.
 * Returns 0 if successful, or a negative value in case of error.
 */
int
load_config(const Bool read_user_config)
{
	int err;

	/* Allocate a place to put the user's configuration */
	if ((sync_config = new_sync_config()) == NULL)
	{
		Error(_("Can't allocate new configuration."));
		return -1;
	}

	/* Add a default conduit to the head of the queue, equivalent
	 * to:
	 * 	conduit sync {
	 *		type: * / *;
	 *		path: [generic];
	 *		default;
	 *	}
	 */
	{
		conduit_block *fallback;	/* The generic default
						 * conduit */

		/* Allocate a new conduit block */
		if ((fallback = new_conduit_block()) == NULL)
		{
			Error(_("Can't allocate new conduit block."));
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
			Error(_("Couldn't initialize configuration."));
			Perror("strdup");

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
			Error(_("Couldn't read configuration file \"%s\"."),
			      DEFAULT_GLOBAL_CONFIG);
			free_sync_config(sync_config);
			sync_config = NULL;
			return -1;
		}
	}

	/* Read ~/.coldsyncrc */
	if (read_user_config)
	{
		err = get_userinfo(&userinfo);
		if (err < 0)
		{
			Error(_("Can't get user info."));
			return -1;
		}

		if (global_opts.conf_fname_given &&
		    !exists(global_opts.conf_fname))
		{
			/* A config file was specified on the command line,
			 * but it doesn't exist. Warn and continue with the
			 * defaults.
			 */
			Error(_("Config file \"%s\" doesn't exist."),
			      global_opts.conf_fname);

			return -1;
		}

		if (global_opts.conf_fname == NULL)
		{
			/* Construct the full pathname to ~/.coldsyncrc in
			 * 'conf_fname'.
			 */
			/* XXX - Replace with snprintf() */
			strncpy(conf_fname, userinfo.homedir, MAXPATHLEN);
			strncat(conf_fname, "/.coldsyncrc",
				MAXPATHLEN - strlen(conf_fname));
			global_opts.conf_fname = conf_fname;
		}

		if (exists(global_opts.conf_fname))
		{
			/* We've already checked for the existence of a
			 * user-specified config file above. This test is
			 * so that ColdSync won't complain if no config
			 * file was specified, and the default
			 * ~/.coldsyncrc doesn't exist.
			 */
			MISC_TRACE(3)
				fprintf(stderr, "Reading \"%s\"\n",
					global_opts.conf_fname);

			err = parse_config_file(global_opts.conf_fname,
						sync_config);
			if (err < 0)
			{
				Error(_("Couldn't read configuration file "
					"\"%s\"."),
				      global_opts.conf_fname);
				free_sync_config(sync_config);
				sync_config = NULL;
				return -1;
			}
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
			Error(_("Can't allocate listen block."));
			free_sync_config(sync_config);
			sync_config = NULL;
			return -1;
		}

		if ((l->device = strdup(global_opts.devname)) == NULL)
		{
			Error(_("Can't copy string."));
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
			Error(_("Can't allocate listen block."));
			free_sync_config(sync_config);
			sync_config = NULL;
			return -1;
		}

		if ((l->device = strdup(PALMDEV)) == NULL)
		{
			Error(_("Can't copy string."));
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
			fprintf(stderr, "\tProtocol: %d\n", l->protocol);
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
static void
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
	else if (strncasecmp(str, "net", 3) == 0)
		net_trace = lvl;
	else {
		Error(_("Unknown facility \"%s\"."), str);
	}
}

/* set_mode
 * Given a mode string, set the mode indicated by the string. Returns 0 if
 * successful, or a negative value in case of error. Currently, the only
 * error is "unknown mode".
 */
static int
set_mode(const char *str)
{
	if (str == NULL)	/* Sanity check */
	{
		/* This should never happen */
		Error(_("%s: Missing mode string. This should never happen."),
		      "set_mode");
		return -1;
	}

	/* Sanity check: if the user specifies the mode twice, complain. */
	if (global_opts.mode != mode_None)
		Warn(_("You shouldn't specify two -m<mode> options.\n"
		       "\tUsing -m%s."),
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

	    case 'I':		/* Init mode */
		global_opts.mode = mode_Init;
		return 0;

	    case 'd':		/* Daemon mode */
		global_opts.mode = mode_Daemon;
		return 0;

	    default:
		Error(_("Unknown mode: \"%s\"."), str);
		return -1;
	}
}

/* usage
 * Print out a usage string.
 */
/* ARGSUSED */
static void
usage(int argc, char *argv[])
{
	/* XXX - gcc 3.0 complains that this string is longer than 509
	 * characters, which is what ISO C89 mandates.
	 */
	printf(_("Usage: %s [options] <mode> <mode args>\n"
		 "Modes:\n"
		 "\t-ms:\tSynchronize (default).\n"
		 "\t-mI:\tInitialize.\n"
		 "\t-mb <dir> [database...]\n"
		 "\t\tPerform a backup to <dir>.\n"
		 "\t-mr <file|dir>...\n"
		 "\t\tRestore or install new databases.\n"
		 "Options:\n"
		 "\t-h:\t\tPrint this help message and exit.\n"
		 "\t-V:\t\tPrint version and exit.\n"
		 "\t-f <file>:\tRead configuration from <file>\n"
		 "\t-z:\t\tInstall databases after sync.\n"
		 "\t-I:\t\tForce installation of new databases.\n"
		 "\t-S:\t\tForce slow sync.\n"
		 "\t-F:\t\tForce fast sync.\n"
		 "\t-R:\t\tCheck ROM databases.\n"
		 "\t-p <port>:\tListen on device <port>.\n"
		 "\t-t <devtype>:\tPort type [serial|usb|tcp].\n"
		 "\t-s:\t\tLog error messages to syslog.\n"
		 "\t-l: <file>:\tWrite error/debugging messages to <file>.\n"
		 "\t-v:\t\tIncrease verbosity.\n"
		 "\t-d <fac[:level]>:\tSet debugging level.\n"),
	       argv[0]);
}

/* print_version
 * Print out the version of ColdSync.
 */
void
print_version(FILE *outfile)
{
	fprintf(outfile, _("%s version %s.\n"),
	       /* These two strings are defined in "config.h" */
	       PACKAGE,
	       VERSION);
	fprintf(outfile,
		_("ColdSync homepage at http://www.ooblick.com/software/"
		 "coldsync/\n"));
	/* XXX - Ought to print out other information, e.g., copyright,
	 * compile-time flags, optional packages, maybe OS name and
	 * version, who compiled it and when, etc.
	 */
	fprintf(outfile, _("Compile-type options:\n"));

#if WITH_USB
	fprintf(outfile,
_("    WITH_USB: USB support.\n"));
#endif	/* WITH_USB */
#if WITH_EFENCE
	fprintf(outfile,
_("    WITH_EFENCE: buffer overruns will cause a segmentation violation.\n"));
#endif	/* WITH_EFENCE */

#if HAVE_STRCASECMP && HAVE_STRNCASECMP
	fprintf(outfile,
_("    HAVE_STRCASECMP, HAVE_STRNCASECMP: strings are compared without regard\n"
"        to case, whenever possible.\n"));
#endif	/* HAVE_STRCASECMP && HAVE_STRNCASECMP */
	fprintf(outfile,
_("\n"
"    Default global configuration file: %s\n"),
		DEFAULT_GLOBAL_CONFIG);
}

/* get_hostinfo
 * Figures out the hostid for this host, and returns it in 'hostid'.
 * Returns 0 if successful, or a negative value in case of error.
 *
 * NB: AFAICT, there's nothing that says that the hostid has to be its IP
 * address. It only has to be a 32-bit integer, one that's unique among the
 * set of all hosts with which the user will sync.
 */
/* NB: the *_TRACE() statements in this function have no effect, since the
 * "-d" options aren't parsed until after this function is called.
 */
int
get_hostinfo()
{
	int err;
	struct hostent *myaddr;		/* This host's address */

	/* Get the hostname */
	if ((err = gethostname(hostname, MAXHOSTNAMELEN)) < 0)
	{
		Error(_("Can't get host name."));
		Perror("gethostname");
		return -1;
	}
	MISC_TRACE(2)
		fprintf(stderr, "My hostname is \"%s\"\n", hostname);

	if ((myaddr = gethostbyname2(hostname, AF_INET)) == NULL)
	{
		Error(_("Can't look up my address."));
		Perror("gethostbyname");
		return -1;
	}

	/* XXX - There should probably be functions to deal with other
	 * address types (e.g., IPv6). Maybe just hash them down to 4
	 * bytes. Hm... actually, that might work for all address types, so
	 * no need to test for AF_INET specifically.
	 *
	 * Looking at the BIND source, it looks as if gethostbyname() only
	 * returns AF_INET addresses. gethostbyname2(), which appeared in
	 * BIND 4.9.4, takes an address family argument.
	 */
	if (myaddr->h_addrtype != AF_INET)
	{
		Error(_("Hey! This isn't an AF_INET address!"));
		return -1;
	}

	MISC_TRACE(2)
	{
		int i;

		fprintf(stderr, "My addresses:\n");
		for (i = 0; myaddr->h_addr_list[i] != NULL; i++)
		{
			fprintf(stderr, "    %d:\n", i);
			debug_dump(stderr, "\tIP",
				   (ubyte *) myaddr->h_addr_list[i],
				   myaddr->h_length);
		}
	}

	/* Make sure there's at least one address */
	if (myaddr->h_addr_list[0] == NULL)
	{
		Error(_("This host doesn't appear to have an IP address."));
		return -1;
	}

	/* Use the first address as the host ID */
	hostid =
		(((udword) myaddr->h_addr_list[0][0] & 0xff) << 24) |
		(((udword) myaddr->h_addr_list[0][1] & 0xff) << 16) |
		(((udword) myaddr->h_addr_list[0][2] & 0xff) << 8) |
		 ((udword) myaddr->h_addr_list[0][3] & 0xff);
	MISC_TRACE(2)
		fprintf(stderr, "My hostid is 0x%08lx\n", hostid);

	return 0;
}

/* sockaddr_len
 * Return the length of the sockaddr pointed to by 'sa'.
 * Moved out into its own function mainly to concentrate all the #ifdef
 * ugliness in one place.
 */
static int
sockaddr_len(const struct sockaddr *sa)
{
#if HAVE_SOCKADDR_SA_LEN
#  ifndef MAX
#    define MAX(x,y)	((x) > (y) ? (x) : (y))
#  endif	/* MAX */

	/* This system has struct sockaddr.sa_len. Yay! */
	return MAX(sizeof(struct sockaddr), sa->sa_len);
#else
	/* This system doesn't have sockaddr.sa_len, so we have to
		 * infer the size of the current entry from the address
		 * family.
		 */
	switch (sa->sa_family)
	{
#  if HAVE_SOCKADDR6	/* Does this machine support IPv6? */
	    case AF_INET6:
		return sizeof(struct sockaddr_in6);
		break;
#  endif	/* HAVE_SOCKADDR6 */
	    case AF_INET:
		return sizeof(struct sockaddr_in);
		break;
	    default:
		return sizeof(struct sockaddr);
		break;
	}
#endif	/* HAVE_SOCKADDR_SA_LEN */
}

/* get_hostaddrs
 * Get the list of IPv4 and IPv6 addresses that correspond to this host.
 * Sets 'hostaddrs' to this list. Sets 'num_hostaddrs' to the length of the
 * array 'hostaddrs'.
 * Returns 0 if successful, or a negative value in case of error.
 */
int
get_hostaddrs()
{
	int err;
	int sock;		/* Socket, for ioctl() */
	char *buf;		/* Buffer to hold SIOCGIFCONF data */
	char *bufptr;		/* Pointer into 'buf' */
	int buflen;		/* Current length of 'buf' */
	int lastlen;		/* Length returned by last SIOCGIFCONF */
	struct ifconf ifconf;	/* SIOCGIFCONF request */
	int hostaddrs_size;	/* Current size of 'hostaddrs[]' (Not to be
				 * confused with the number of entries it
				 * contains: 'hostaddrs_size' refers to the
				 * amount of memory allocated).
				 */

	free_hostaddrs();	/* In case this isn't the first time this
				 * function was called. */

	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		Error(_("%s: socket() returned %d."),
		      "get_hostaddrs",
		      sock);
		Perror("socket");
		return -1;
	}

	/* Obtain the list of interfaces with ioctl(SIOCGIFCONF).
	 * Unfortunately, the API for SIOCGIFCONF sucks rocks, and isn't
	 * consistent across implementations. The problem is that the
	 * caller supplies a buffer into which to place the results, but
	 * while SIOCGIFCONF returns the size of the data, it doesn't say
	 * whether the buffer was large enough to hold all of the data for
	 * all interfaces (and aliases), nor how big the buffer ought to
	 * be.
	 *
	 * Hence, we resort to the kludge described in UNP[1]: call
	 * SIOCGIFCONF repeatedly, with an increasing buffer size, until
	 * the size of the returned data stops increasing. Thus, even if
	 * our initial buffer size is large enough to hold all of the data,
	 * we still need to SIOCGIFCONF twice to find this out.
	 *
	 * [1] W. Richard Stevens, "Unix Network Programming, vol 1",
	 * section 16.6.
	 */
	lastlen = 0;
	buflen = 2048;		/* Initial guess at buffer size */
	for (;;)
	{
		if ((buf = malloc(buflen)) == NULL)
			/* Out of memory */
			return -1;

		ifconf.ifc_len = buflen;
		ifconf.ifc_buf = buf;

		err = ioctl(sock, SIOCGIFCONF, &ifconf);
		if (err < 0)
		{
			/* Solaris returns -1 and sets errno to EINVAL when
			 * 'buf' is too small to hold the request. FreeBSD
			 * returns 0, fills the buffer with as much data as
			 * will fit, and returns the size of the partial
			 * data. DU doesn't seem to say what it does.
			 */
			if ((errno != EINVAL) || (lastlen != 0))
			{
				/* Something unexpected went wrong */
				Perror("ioctl(SIOCGIFCONF)");
				free(buf);
				return -1;
			}
		} else {
			if ((lastlen != 0) && (ifconf.ifc_len == lastlen))
				/* Success: size hasn't changed */
				break;
			lastlen = ifconf.ifc_len;
					/* Remember size for next iteration */
		}

		/* If we get this far, then the buffer isn't large enough.
		 * Increase its size and try again.
		 */
		free(buf);
		buflen *= 2;
	}

	/* Allocate hostaddrs[] */
	hostaddrs_size = (buflen / sizeof(struct ifreq)) + 1;
					/* Initial guess at number of
					 * addresses contained in 'buf'.
					 */
	if ((hostaddrs = (struct sockaddr **)
	     malloc(hostaddrs_size * sizeof(struct sockaddr *))) == NULL)
	{
		Error(_("%s: Out of memory."),
		      "get_hostaddrs");
		free(buf);
		return -1;
	}

	/* Go through 'buf' and get each interface's address in turn. 'buf'
	 * contains an array of 'struct ifreq's. However, 'struct ifreq' is
	 * a variable-sized union that consists of a (fixed size) name
	 * followed by a variable-size 'struct ifaddr'. Hence, it takes a
	 * bit of effort to find the address of the (n+1)th element of the
	 * list, given the location of the nth element.
	 */
	num_hostaddrs = 0;
	for (bufptr = buf; bufptr < buf + ifconf.ifc_len;)
	{
		struct ifreq *ifreq;	/* Current interface element */
		int sa_len;		/* Length of current sockaddr */

		ifreq = (struct ifreq *) bufptr;

		/* Point 'bufptr' to the next entry */
		sa_len = sockaddr_len(&(ifreq->ifr_addr));
		bufptr += sizeof(ifreq->ifr_name) + sa_len;

		/* Make sure 'hostaddrs' is large enough to contain
		 * the next entry.
		 */
		if (hostaddrs_size <= num_hostaddrs)
		{
			struct sockaddr *temp;

			hostaddrs_size *= 2;
			temp = realloc(hostaddrs, hostaddrs_size);
			if (temp == NULL)
			{
				Error(_("%s: Out of memory."),
				      "get_hostaddrs");
				free(hostaddrs);
				hostaddrs = NULL;
				free(buf);
				return -1;
			}
		}

		switch (ifreq->ifr_addr.sa_family)
		{
		    case AF_INET:	/* IPv4 */
#if HAVE_SOCKADDR6
		    case AF_INET6:	/* IPv6 */
#endif	/* HAVE_SOCKADDR6 */
			{
				struct sockaddr *addr;

				/* Copy current sockaddr to 'hostaddrs[]' */
				if ((addr = (struct sockaddr *)
				     malloc(sa_len)) == NULL)
				{
					Error(_("%s: Out of memory."),
					      "get_hostaddrs");
					free(hostaddrs);
					hostaddrs = NULL;
					free(buf);
					return -1;
				}
				memcpy(addr, &(ifreq->ifr_addr), sa_len);
				hostaddrs[num_hostaddrs] = addr;

#if 0
{ char data[1024];
switch (ifreq->ifr_addr.sa_family)
{
    case AF_INET:
	{
		struct sockaddr_in *sa;

		sa = (struct sockaddr_in *) hostaddrs[num_hostaddrs];
		MISC_TRACE(6)
			fprintf(stderr, "Interface [%s]: [%s]\n",
				ifreq->ifr_name,
				inet_ntop(sa->sin_family,
					  &sa->sin_addr,
					  data, 1024));
	}
	break;
    case AF_INET6:
	{
		struct sockaddr_in6 *sa;

		sa = (struct sockaddr_in6 *) hostaddrs[num_hostaddrs];
		MISC_TRACE(6)
			fprintf(stderr, "Interface [%s]: [%s]\n",
				ifreq->ifr_name,
				inet_ntop(sa->sin6_family,
					  &(sa->sin6_addr),
					  data, 1024));
	}
	break;
    default:
	break;
}
}
#endif	/* 0 */
				num_hostaddrs++;
			}
			break;
		    default:
			/* Ignore anything else */
			break;
		}
	}

	/* If 'hostaddrs' was resized larger than it needs to be, realloc()
	 * it to the proper size (let's not waste memory).
	 */
	if (hostaddrs_size > num_hostaddrs)
	{
		struct sockaddr **temp;

		temp = (struct sockaddr **)
			realloc(hostaddrs,
				num_hostaddrs * sizeof(struct sockaddr *));
		if (temp == NULL)
		{
			Error(_("%s: realloc(%d) failed."),
			      "get_hostaddrs",
			      num_hostaddrs * sizeof(struct sockaddr *));
			free(hostaddrs);
			hostaddrs = NULL;
			free(buf);
			return -1;
		}
		hostaddrs = temp;
	}
#if 0
MISC_TRACE(4)
{
int i;

fprintf(stderr, "Final list of addresses, hostaddrs == 0x%08lx:\n",
	(unsigned long) hostaddrs);
for (i = 0; i < num_hostaddrs; i++)
{
	fprintf(stderr, "hostaddrs[%d] == 0x%08lx\n",
		i,
		(unsigned long) hostaddrs[i]);
}
}
#endif	/* 0 */

	free(buf);
	return 0;		/* Success */
}

/* free_hostaddrs
 * Free the 'hostaddrs' array.
 */
void
free_hostaddrs(void)
{
	int i;

	if (hostaddrs == NULL)
		return;
	for (i = 0; i < num_hostaddrs; i++)
		if (hostaddrs[i] != NULL)
			free(hostaddrs[i]);
	free(hostaddrs);
	hostaddrs = NULL;	/* Belt and suspenders */
}

/* print_pda_block
 * Print to 'outfile' a suggested PDA block that would go in a user's
 * .coldsyncrc . The values in the suggested PDA block come from 'pda' and
 * 'palm'.
 */
void
print_pda_block(FILE *outfile, const pda_block *pda, struct Palm *palm)
{
	udword p_userid;
	const char *p_username;
	const char *p_snum;
	int p_snum_len;

	/* Get length of serial number */
	p_snum_len = palm_serial_len(palm);
	if (p_snum_len < 0)
		return;		/* Something went wrong */

	/* Get serial number */
	p_snum = palm_serial(palm);
	if ((p_snum == NULL) && (cs_errno != CSE_NOERR))
		return;

	/* Get userid */
	p_userid = palm_userid(palm);
	if ((p_userid == 0) && (cs_errno != CSE_NOERR))
		return;		/* Something went wrong */

	/* Get username */
	p_username = palm_username(palm);
	if ((p_username == NULL) && (cs_errno != CSE_NOERR))
		return;		/* Something went wrong */

	/* First line of PDA block */
	if ((pda == NULL) || (pda->name == NULL) ||
	    (pda->name[0] == '\0'))
		printf("pda {\n");
	else
		printf("pda \"%s\" {\n", pda->name);

	/* "snum:" line in PDA block */
	if (p_snum == NULL)
		;	/* Omit the "snum:" line entirely */
	else if (p_snum[0] == '\0')
	{
		/* This Palm doesn't have a serial number. Say so. */
		printf("\tsnum: \"\";\n");
	} else if (p_snum[0] == '*') {
		/* Print the Palm's serial number. */
		/* Special serial numbers (currently, only "*Visor*") are
		 * assumed to begin with '*', so anything that begins with
		 * '*' doesn't get a checksum.
		 */
		printf("\tsnum: \"%s\";\n", p_snum);
	} else {
		/* Print the Palm's serial number. */
		printf("\tsnum: \"%s-%c\";\n",
		       p_snum,
		       snum_checksum(p_snum, p_snum_len));
	}

	/* "directory:" line in PDA block */
	if ((pda != NULL) && (pda->directory != NULL) &&
	    (pda->directory[0] != '\0'))
		printf("\tdirectory: \"%s\";\n", pda->directory);

	/* "username:" line in PDA block */
	if ((p_username == NULL) || (p_username[0] == '\0'))
		printf("\tusername: \"%s\";\n", userinfo.fullname);
	else
		printf("\tusername: \"%s\";\n", p_username);

	/* "userid:" line in PDA block */
	if (p_userid == 0)
		printf("\tuserid: %ld;\n",
		       (long) userinfo.uid);
	else
		printf("\tuserid: %ld;\n", p_userid);

	/* PDA block closing brace. */
	printf("}\n");
}

/* name2listen_type
 * Convert the name of a listen type to its integer value. See the LISTEN_*
 * defines in "pconn/PConnection.h".
 */
static int
name2listen_type(const char *str)
{
	/* XXX - It'd be really nice if these strings were translatable */
	if (strcasecmp(str, "serial") == 0)
		return LISTEN_SERIAL;
	if (strcasecmp(str, "net") == 0)
		return LISTEN_NET;
	if (strcasecmp(str, "usb") == 0)
		return LISTEN_USB;
	if (strcasecmp(str, "usb_m50x") == 0)
		return LISTEN_USB_M50x;
	return -1;		/* None of the above */
}

/* name2protocol
 * Given the name of a software protocol stack keyword, convert it to its
 * corresponding integer value. See the PCONN_STACK_* defines in
 * "pconn/PConnection.h"
 */
static int
name2protocol(const char *str)
{
	if (strcasecmp(str, "default") == 0)
		return PCONN_STACK_DEFAULT;
	if (strcasecmp(str, "full") == 0)
		return PCONN_STACK_FULL;
	if ((strcasecmp(str, "simple") == 0) ||
	    (strcasecmp(str, "m50x") == 0))
		return PCONN_STACK_SIMPLE;
	if (strcasecmp(str, "net") == 0)
		return PCONN_STACK_NET;
	return -1;		/* None of the above */
}

/* find_pda_block
 * Helper function. Finds the best-matching PDA block for the given Palm.
 * Returns a pointer to it if one was defined in the config file(s), or
 * NULL otherwise.
 *
 * If 'check_user' is false, find_pda_block() only checks the serial
 * number. If 'check_user' is true, it also checks the user name and ID. If
 * the user name or ID aren't set in a PDA block, they act as wildcards, as
 * far as find_pda_block() is concerned.
 */
pda_block *
find_pda_block(struct Palm *palm, const Bool check_user)
{
	pda_block *cur;		/* The pda-block we're currently looking at */
	pda_block *default_pda;	/* pda_block for the default PDA, if no
				 * better match is found.
				 */
	const char *p_snum;	/* Serial number of Palm */

	MISC_TRACE(4)
	{
		fprintf(stderr, "Looking for a PDA block.\n");
		if (check_user)
			fprintf(stderr, "  Also checking user info\n");
		else
			fprintf(stderr, "  But not checking user info\n");
	}

	/* Get serial number */
	p_snum = palm_serial(palm);
	if ((p_snum == NULL) && (cs_errno != CSE_NOERR))
		return NULL;

	default_pda = NULL;
	for (cur = sync_config->pda; cur != NULL; cur = cur->next)
	{
		MISC_TRACE(5)
			fprintf(stderr, "Checking PDA \"%s\"\n",
				(cur->name == NULL ? "" : cur->name));

		/* See if this pda_block has a serial number and if so,
		 * whether it matches the one we read off of the Palm.
		 * Palms with pre-3.0 ROMs don't have serial numbers. They
		 * can be represented in the .coldsyncrc file with
		 *	snum "";
		 * This does mean that you shouldn't have more than one
		 * pda_block with an empty string.
		 */
		/* XXX - Bug: I've gotten a core dump here, where p_snum ==
		 * NULL.
		 */
		if ((cur->snum != NULL) &&
		    (strncasecmp(cur->snum, p_snum, SNUM_MAX)
		     != 0))
		{
			/* The serial number doesn't match */
			MISC_TRACE(5)
				fprintf(stderr,
					"\tSerial number doesn't match\n");

			continue;
		}

		/* XXX - Ought to see if the serial number is a real one,
		 * or if it's binary bogosity (on the Visor).
		 */

		/* Check the username and userid, if asked */
		if (check_user)
		{
			const char *p_username;	/* Username on Palm */
			udword p_userid;	/* User ID on Palm */

			/* Check the user ID */
			p_userid = palm_userid(palm);
			if ((p_userid == 0) && (cs_errno != CSE_NOERR))
			{
				/* Something went wrong */
				return NULL;
			}

			if (cur->userid_given &&
			    (cur->userid != p_userid))
			{
				MISC_TRACE(5)
					fprintf(stderr,
						"\tUserid doesn't match\n");
				continue;
			}

			/* Check the user name */
			p_username = palm_username(palm);
			if ((p_username == NULL) && (cs_errno != CSE_NOERR))
			{
				/* Something went wrong */
				return NULL;
			}

			if ((cur->username != NULL) &&
			    strncmp(cur->username, p_username,
				    DLPCMD_USERNAME_LEN) != 0)
			{
				MISC_TRACE(5)
					fprintf(stderr,
						"\tUsername doesn't match\n");
				continue;
			}
		}

		MISC_TRACE(3)
		{
			fprintf(stderr, "Found a match for this PDA:\n");
			fprintf(stderr, "\tS/N: [%s]\n", cur->snum);
			fprintf(stderr, "\tDirectory: [%s]\n",
				cur->directory);
		}

		if ((cur->flags & PDAFL_DEFAULT) != 0)
		{
			MISC_TRACE(3)
				fprintf(stderr,
					"This is a default PDA\n");

			/* Mark this as the default pda_block */
			default_pda = cur;
			continue;
		}

		/* If we get this far, then the serial number matches and
		 * this is not a default pda_block. So this is the one we
		 * want to use.
		 */
		return cur;
	}

	/* If we get this far, then there's no non-default matching PDA. */
	MISC_TRACE(3)
		fprintf(stderr, "No exact match found for "
			"this PDA. Using default\n");

	return default_pda;
			/* 'default_pda' may or may not be NULL. In either
			 * case, this does the Right Thing: if it's
			 * non-NULL, then this function returns the default
			 * PDA block. If 'default_pda' is NULL, then the
			 * config file contains neither a good matching
			 * PDA, nor a default, so it should return NULL
			 * anyway.
			 */
}

/* make_sync_dirs
 * Make sure that the various directories that ColdSync will be using for
 * the PDA 'pda' exist; create them if necessary:
 *	<basedir>
 *	<basedir>/archive
 *	<basedir>/backup
 *	<basedir>/install
 * ('basedir' will be ~/.palm by default)
 *
 * XXX - This also initializes the global variables 'backupdir',
 * 'atticdir', 'archivedir', and 'installdir'. This seems nonintuitive.
 *
 * Returns 0 if successful, or a negative value in case of error.
 */
int
make_sync_dirs(const char *basedir)
{
	int err;

	MISC_TRACE(3)
		fprintf(stderr, "Creating backup directories.\n");

	/* ~/.palm */
	if (!is_directory(basedir))
	{
		/* ~/.palm doesn't exist. Create it. */
		MISC_TRACE(4)
			fprintf(stderr, "mkdir(%s)\n", basedir);

		if ((err = mkdir(basedir, DIR_MODE)) < 0)
		{
			Error(_("Can't create base sync directory %s."),
			      basedir);
			Perror("mkdir");
			return -1;
		}
	}

	/* ~/.palm/backup */
	strncpy(backupdir, mkfname(basedir, "/backup", NULL), MAXPATHLEN);
	backupdir[MAXPATHLEN] = '\0';
	if (!is_directory(backupdir))
	{
		/* ~/.palm/backup doesn't exist. Create it. */
		MISC_TRACE(4)
			fprintf(stderr, "mkdir(%s)\n", backupdir);

		if ((err = mkdir(backupdir, DIR_MODE)) < 0)
		{
			Error(_("Can't create backup directory %s."),
			      backupdir);
			Perror("mkdir");
			return -1;
		}
	}

	/* ~/.palm/backup/Attic */
	strncpy(atticdir, mkfname(basedir, "/backup/Attic", NULL), MAXPATHLEN);
	atticdir[MAXPATHLEN] = '\0';
	if (!is_directory(atticdir))
	{
		/* ~/.palm/backup doesn't exist. Create it. */
		MISC_TRACE(4)
			fprintf(stderr, "mkdir(%s)\n", atticdir);

		if ((err = mkdir(atticdir, DIR_MODE)) < 0)
		{
			Error(_("Can't create attic directory %s."),
			      atticdir);
			Perror("mkdir");
			return -1;
		}
	}

	/* ~/.palm/archive */
	strncpy(archivedir, mkfname(basedir, "/archive", NULL), MAXPATHLEN);
	archivedir[MAXPATHLEN] = '\0';
	if (!is_directory(archivedir))
	{
		/* ~/.palm/archive doesn't exist. Create it. */
		MISC_TRACE(4)
			fprintf(stderr, "mkdir(%s)\n", archivedir);

		if ((err = mkdir(archivedir, DIR_MODE)) < 0)
		{
			Error(_("Can't create archive directory %s."),
			      archivedir);
			Perror("mkdir");
			return -1;
		}
	}

	/* ~/.palm/install */
	strncpy(installdir, mkfname(basedir, "/install", NULL), MAXPATHLEN);
	installdir[MAXPATHLEN] = '\0';
	if (!is_directory(installdir))
	{
		/* ~/.palm/install doesn't exist. Create it. */
		MISC_TRACE(4)
			fprintf(stderr, "mkdir(%s)\n", installdir);

		if ((err = mkdir(installdir, DIR_MODE)) < 0)
		{
			Error(_("Can't create install directory %s."),
			      installdir);
			Perror("mkdir");
			return -1;
		}
	}

	return 0;		/* Success */
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
			"Allocated sync_config %p\n", (void *) retval);
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
		fprintf(stderr, "Freeing sync_config %p\n", (void *) config);

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

	uid = getuid();
	userinfo->uid = uid;

	MISC_TRACE(2)
		fprintf(stderr, "UID: %lu, euid %lu\n",
			(unsigned long) uid,
			(unsigned long) geteuid());

	/* Get the user's password file info */
	if ((pwent = getpwuid(uid)) == NULL)
	{
		Perror("get_userinfo: getpwuid");
		return -1;
	}

	/* Get the user's full name */
	if (get_fullname(userinfo->fullname, sizeof(userinfo->fullname),
			 pwent) < 0)
	{
		Error(_("Can't get user's full name."));
		return -1;
	}

	/* Get the user's home directory
	 * Don't use the $HOME environment variable: under FreeBSD, when
	 * usbd is started, it gets HOME=/ . This is passed down to the
	 * child processes. Plus, the UID and EUID are the same, so you
	 * can't even use that to distinguish this particular case.
	 * And besides, the benefit you get from being able to override
	 * $HOME is minuscule.
	 */
	strncpy(userinfo->homedir, pwent->pw_dir,
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
	retval->protocol = PCONN_STACK_DEFAULT;
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
		if (hdr->name != NULL)
			free(hdr->name);
		if (hdr->value != NULL)
			free(hdr->value);
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
	retval->flags = 0;
	retval->name = NULL;
	retval->snum = NULL;
	retval->directory = NULL;
	retval->username = NULL;
	retval->userid_given = False;
	retval->userid = 0L;
	retval->forward = False;
	retval->forward_host = NULL;
	retval->forward_name = NULL;

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
	if (p->forward_host != NULL)
		free(p->forward_host);
	if (p->forward_name != NULL)
		free(p->forward_name);
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
