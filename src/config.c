/* config.c
 *
 * Functions dealing with loading the configuration.
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: config.c,v 1.106 2002-10-26 12:05:12 azummo Exp $
 */
#include "config.h"
#include <stdio.h>
#include <unistd.h>		/* For getuid(), gethostname(), getopt() */
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

#if HAVE_GETOPT_LONG
#include <getopt.h>		/* For GNU getopt_long() */
#endif	/* HAVE_GETOPT_LONG */

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
#if !HAVE_GETOPT_LONG
#  define getopt_long(argc, argv, shortopts, longopts, longind) \
	getopt(argc, argv, shortopts)
#endif	/* !HAVE_GETOPT_LONG */

extern struct config config;

/* For debugging only */
extern void debug_dump(FILE *outfile, const char *prefix,
		       const ubyte *buf, const udword len);

/* XXX - This should probably be hidden inside a "struct config{..}" or
 * something. I don't like global variables.
 */
udword hostid = 0L;		/* This machine's host ID, so you can tell
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
char rescuedir[MAXPATHLEN+1];	/* ~/.palm/rescue pathname */

char conf_fname[MAXPATHLEN+1];	/* ~/.coldsyncrc */

struct userinfo userinfo;	/* Information about the Palm's owner */
	/* XXX - Probably should go in sync_config */

static void set_debug_level(const char *str);
static int set_mode(const char *str);
static void usage(int argc, char *argv[]);
static pconn_proto_t name2protocol(const char *str);
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

#if HAVE_GETOPT_LONG

	/* Options: */

	static struct option longopts[] =
	{
		{"mode", 		required_argument, 	NULL, 'm'},
		{"help", 		no_argument,		NULL, 'h'},
		{"version",		no_argument,		NULL, 'V'},
		{"verbose",		no_argument,		NULL, 'v'},
		{"config",		required_argument,	NULL, 'f'},
		{"slow-sync",		no_argument,		NULL, 'S'},
		{"fast-sync",		no_argument,		NULL, 'F'},
		{"force-install",	no_argument,		NULL, 'I'},
		{"install-first",	no_argument,		NULL, 'z'},
		{"consider-readonly",	no_argument,		NULL, 'R'},
		{"device",		required_argument,	NULL, 'p'},
		{"device-type",		required_argument,	NULL, 't'},
		{"protocol",		required_argument,	NULL, 'P'},
		{"use-syslog",		no_argument,		NULL, 's'},
		{"logfile",		required_argument,	NULL, 'l'},
		{"debug",		required_argument,	NULL, 'd'},
		{"auto-init",		no_argument,		NULL, 'a'},
		{"listen-block",	required_argument,	NULL, 'n'},
		/* XXX - Would it be possible to have translated versions
		 * of the long options here as well? In some cases, the
		 * translated version remains the same ("--mode" stays
		 * "--mode" in French). It's also possible that when you
		 * translate one option, you'll get the same string as a
		 * different English option (I can't come up with any
		 * examples right now).
		 *
		 * How does getopt_long() handle duplicate options? If it
		 * takes the first one it finds, everything's fine.
		 *
		 * Note that if there's any ambiguity, we should prefer the
		 * English option string: that way, scripts that run
		 * coldsync will be portable. Of course, portable scripts
		 * shouldn't be using translated option strings in the
		 * first place; they can use comments for the translation.
		 *
		 * And what about Japanese? Will we need iconv()? Ugh.
		 *
		 * azummo: I don't think options needs to be translated
		 *  ... and would be a nightmare to maintain.
		 */
	};	 
#endif	/* HAVE_GETOPT_LONG */
	 
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

#if HAVE_GETOPT_LONG
	while ((arg = getopt_long(argc, argv, ":hvVSFRIaszf:l:m:p:t:P:d:n:",
			&longopts[0], NULL))
	       != -1)
#else
	while ((arg = getopt(argc, argv, ":hvVSFRIaszf:l:m:p:t:P:d:n:"))
	       != -1)
#endif
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
			global_opts.force_install = True3;
			break;

		    case 'a':	/* -a: Auutoinitialize palm in daemon mode */
			global_opts.autoinit = True3;
			break;

		    case 'z':	/* -z: Install databases after main sync */
			global_opts.install_first = False3;
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
			global_opts.log_fname = optarg;
			if (put_symbol("LOGFILE", optarg) < 0)
				/* XXX - Now would also be a good time to
				 * set 'final' on the LOGFILE symbol: if it
				 * was specified on the command line, that
				 * overrides any value given in the
				 * environment or config file(s).
				 */
				return -1;
			break;

		    case 'm':	/* -m <mode>: Run in the given mode */
			err = set_mode(optarg);
			if (err < 0)
			{
				usage(argc, argv);
				return -1;
			}
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
			if (global_opts.devtype == LISTEN_NONE)
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
			if (global_opts.protocol == PCONN_STACK_NONE)
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

		    case 'n':	/* -n <listen block name>: Use the named listen 
		    		 * block.
		    		 */
		    	global_opts.listen_name = optarg;
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

	/* Initialize the options to be undefined */
	sync_config->options.force_install	= Undefined;
	sync_config->options.install_first	= Undefined;
	sync_config->options.autoinit		= Undefined;
	sync_config->options.autorescue		= False;
	sync_config->options.filter_dbs		= False;	 /* We don't have an equivalent cmd line option
								  * for the last twos, so they defaults to 
								  * False here.
								  */

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
		append_crea_type(fallback, 0x0000, 0x0000, 0x00);
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

	/* Set options from the config file(s) that don't override the
	 * command line.
	 */
	if (global_opts.install_first == Undefined)
	{
		if (sync_config->options.install_first == Undefined)
			global_opts.install_first = True3;
		else
			global_opts.install_first =
				sync_config->options.install_first;
	}

	if (global_opts.force_install == Undefined)
	{
		if (sync_config->options.force_install == Undefined)
			global_opts.force_install = False3;
		else
			global_opts.force_install =
				sync_config->options.force_install;
	}

	if (global_opts.autoinit == Undefined)
	{
		if (sync_config->options.autoinit == Undefined)
			global_opts.autoinit = False3;
		else
			global_opts.autoinit =
				sync_config->options.autoinit;
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
	
		if (prepend_listen_block(global_opts.devname, global_opts.devtype, global_opts.protocol) < 0)
		{
			free_sync_config(sync_config);
			sync_config = NULL;
			return -1;
		}
	}

	if (sync_config->listen == NULL)
	{
		/* No device specified either on the command line or in the
		 * config files. Create a new, default listen block.
		 */

		MISC_TRACE(4)
			fprintf(stderr, "No device specified on the "
				"command line or in config file.\n"
				"Using default: \""
				PALMDEV "\"\n");

		if (prepend_listen_block(PALMDEV, global_opts.devtype, global_opts.protocol) < 0)
		{
			free_sync_config(sync_config);
			sync_config = NULL;
			return -1;
		}
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
			fprintf(stderr, "\tName: %s\n", l->name ? l->name : "(unnamed)");
			fprintf(stderr, "\tType: %d\n", (int) l->listen_type);
			fprintf(stderr, "\tDevice: [%s]\n", l->device);
			fprintf(stderr, "\tSpeed: %ld\n", l->speed);
			fprintf(stderr, "\tProtocol: %d\n", (int) l->protocol);
			fprintf(stderr, "\tFlags:");
			if ((l->flags & LISTENFL_TRANSIENT) != 0)
				fprintf(stderr, " TRANSIENT");
			fprintf(stderr, "\n");
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
				register unsigned char flags = c->ctypes[i].flags;

				fprintf(stderr,
					"\t  [%c%c%c%c/%c%c%c%c] "
					"(0x%08lx/0x%08lx) (flags: %02x)\n",
					(char) ((crea >> 24) & 0xff),
					(char) ((crea >> 16) & 0xff),
					(char) ((crea >> 8) & 0xff),
					(char) (crea & 0xff),
					(char) ((type >> 24) & 0xff),
					(char) ((type >> 16) & 0xff),
					(char) ((type >> 8) & 0xff),
					(char) (type & 0xff),
					crea,
					type,
					flags);
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
	else if (strncasecmp(str, "conduit", 7) == 0)
		conduit_trace = lvl;
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

	if (strlen(str) == 1) /* Short mode name */
	{
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
	else /* Long mode name */
	{
		if (strcmp(str,"standalone") == 0)
		{
			global_opts.mode = mode_Standalone;
			return 0;
		}
		else if (strcmp(str,"backup") == 0)
		{
			global_opts.mode = mode_Backup;
			return 0;
		}
		else if (strcmp(str,"restore") == 0)
		{
			global_opts.mode = mode_Restore;
			return 0;
		}
		else if (strcmp(str,"init") == 0)
		{
			global_opts.mode = mode_Init;
			return 0;
		}
		else if (strcmp(str,"daemon") == 0)
		{
			global_opts.mode = mode_Daemon;
			return 0;
		}
		else
		{
			Error(_("Unknown mode: \"%s\"."), str);
			return -1;
		}		
	}
}

/* usage
 * Print out a usage string.
 */
/* ARGSUSED */
static void
usage(int argc, char *argv[])
{
	/* The usage string is broken up into individual strings in
	 * usage_msg[] for two reasons:
	 * First of all, gcc 3.0 complains if the string is longer than 509
	 * characters, which is what ISO C89 mandates.
	 *
	 * Secondly, a single monolithic string is harder to translate: if
	 * you add one option, suddenly the whole usage string, in other
	 * languages, becomes obsolete.
	 * By breaking this up one string per option, if you add an option
	 * and don't translate it immediately, then non-English users will
	 * see most of the usage string in their language, and the new
	 * option in English. Not perfect, but better than seeing the whole
	 * thing in English.
	 */
	/* XXX - This still isn't quite right: it would be nice to have
	 * arguments: in the string
	 *	-t <devtype>: Port type [serial|usb|net].
	 * the substring "serial|usb|net" should not be translated. Hence,
	 * it would be nice to move it out of the translated string, so
	 * that the range of values can be changed without having to be
	 * retranslated every time.
	 * Any suggestions?
	 *
	 * Perhaps something like the following:
	 * static char *usage_msg[][3] = {
	 *	{ "plain\n" },
	 *	{ "protocol [%s]\n", "serial|usb|net" },
	 *	{ "plain 2\n" },
	 *	{ NULL }
	 * };
	 * int i;
	 *
	 * for (i = 0; usage_msg[i][0] != NULL; i++)
	 * {
	 *	vprintf(usage_msg[i][0], (void *) &usage_msg[i][1]);
	 * }
	 */
	/* XXX - This should also print long options if they're enabled.
	 * I'm not sure how to handle this, though. In particular, for
	 * those options that take an argument, the argument should be
	 * translated. And in an ideal world, some or all of the long
	 * options would be translated as well, and coldsync would accept
	 * either the English or translated version. Thus, in French
	 * locales, it should check for
	 *	-f <file>
	 *	--file <file>
	 *	--fichier <file>
	 * (in that order: that way, people can write portable scripts by
	 * using the English options) and the usage message should list
	 *	-f <fichier>
	 *	--file <fichier>
	 *	--fichier <fichier>
	 * if long options are enabled, or
	 *	-f <fichier>
	 * if long options aren't enabled. (Or is there an "=" in there
	 * somewhere?)
	 */
	const char *usage_msg[] = {
		N_("Modes:\n"),
		N_("\t-ms:\tSynchronize (default).\n"),
		N_("\t-mI:\tInitialize.\n"),
		N_("\t-mb <dir> [database...]\n"
		   "\t\tPerform a backup to <dir>.\n"),
		N_("\t-mr <file|dir>...\n"
		   "\t\tRestore or install new databases.\n"),
		N_("Options:\n"),
		N_("\t-h:\t\tPrint this help message and exit.\n"),
		N_("\t-V:\t\tPrint version and exit.\n"),
		N_("\t-f <file>:\tRead configuration from <file>\n"),
		N_("\t-z:\t\tInstall databases after sync.\n"),
		N_("\t-I:\t\tForce installation of new databases.\n"),
		N_("\t-S:\t\tForce slow sync.\n"),
		N_("\t-F:\t\tForce fast sync.\n"),
		N_("\t-R:\t\tCheck ROM databases.\n"),
		N_("\t-p <port>:\tListen on device <port>.\n"),
		N_("\t-t <devtype>:\tPort type [serial|usb|net].\n"),
		N_("\t-P <protocol>:\tSoftware protocol "
		   "[default|full|simple|net].\n"),
		N_("\t-s:\t\tLog error messages to syslog.\n"),
		N_("\t-l: <file>:\tWrite error/debugging messages to "
		   "<file>.\n"),
		N_("\t-v:\t\tIncrease verbosity.\n"),
		N_("\t-n <listen-block>:\tChoice the named listen block.\n"),
		N_("\t-d <fac[:level]>:\tSet debugging level.\n"),
		NULL
	};
	int i;

	printf(_("Usage: %s [options] <mode> <mode args>\n"), argv[0]);
	for (i = 0; usage_msg[i] != NULL; i++)
		printf("%s", _(usage_msg[i]));
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
		_("ColdSync homepage at http://www.coldsync.org/"
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
	/* XXX - IPv6 */
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

/* name2protocol
 * Given the name of a software protocol stack keyword, convert it to its
 * corresponding integer value. See the PCONN_STACK_* defines in
 * "pconn/PConnection.h"
 */
static pconn_proto_t
name2protocol(const char *str)
{
	if (strcasecmp(str, "default") == 0)
		return PCONN_STACK_DEFAULT;
	if (strcasecmp(str, "full") == 0)
		return PCONN_STACK_FULL;
	if (strcasecmp(str, "simple") == 0)
		return PCONN_STACK_SIMPLE;
	if (strcasecmp(str, "net") == 0)
		return PCONN_STACK_NET;
	return PCONN_STACK_NONE;		/* None of the above */
}

/* make_sync_dirs
 * Make sure that the various directories that ColdSync will be using for
 * the PDA 'pda' exist; create them if necessary:
 *	<basedir>
 *	<basedir>/archive
 *	<basedir>/backup
 *	<basedir>/install
 *	<basedir>/rescue
 * ('basedir' will be ~/.palm by default)
 *
 * XXX - This also initializes the global variables 'backupdir',
 * 'atticdir', 'archivedir', 'installdir' and 'rescuedir'. This seems nonintuitive.
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

	/* ~/.palm/rescue */
	strncpy(rescuedir, mkfname(basedir, "/rescue", NULL), MAXPATHLEN);
	rescuedir[MAXPATHLEN] = '\0';
	if (!is_directory(rescuedir))
	{
		/* ~/.palm/rescue doesn't exist. Create it. */
		MISC_TRACE(4)
			fprintf(stderr, "mkdir(%s)\n", rescuedir);

		if ((err = mkdir(rescuedir, DIR_MODE)) < 0)
		{
			Error(_("Can't create rescue directory %s."),
			      rescuedir);
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

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
