/* spalm.h
 *
 * Declarations and definitions pertaining to 'struct Palm's.
 *
 *	Copyright (C) 2000-2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: spalm.h,v 2.1 2001-06-26 06:18:52 arensb Exp $
 */
#ifndef _spalm_h_
#define _spalm_h_

#include "pconn/pconn.h"
#include "palm.h"

#define SNUM_MAX	32		/* Max. length of serial number
					 * information, including the
					 * terminating NUL that we'll add.
					 * AFAIK, all Palms have 12-digit
					 * serial numbers, but hey, it
					 * might change.
					 */

/* struct Palm
 * A 'struct Palm' is a local cached representation of information
 * about the Palm device at the other end of a PConnection.
 *
 * It is implemented as an instance of the Proxy design pattern[1]:
 * all of the information about the Palm is retrieved via accessor
 * functions, not by directly accessing the structure's fields[2]. If
 * the information in question has not yet been fetched from the Palm,
 * the accessor function does so. Thus, information is only fetched if
 * and when it is needed, which should lead to clearer and more
 * efficient code.
 *
 * NB: All of the members of this struct are private! Don't touch them
 * outside of "spalm.c"!
 *
 * [1] Erich Gamma, Richard Helm, Ralph Johnson, John Vlissides,
 * "Design Patterns," Addison-Wesley.
 *
 * [2] "[Perl] would prefer that you stayed out of its living room
 * because you weren't invited, not because it has a shotgun."
 *	-- perlmodlib(1).
 * The same applies to 'struct Palm'.
 */
/* XXX - Might be good to include a 'palm_disconnect()' function that
 * sets theh Palm's PConnection to NULL, so that later functions don't
 * try to talk to a Palm that isn't there.
 */
/* XXX - Need an 'int palm_ok()' function that indicates whether the
 * last accessor worked fine, or whether there was an error.
 */
struct Palm
{
	PConnection *pconn_;		/* Connection to the Palm */
	Bool have_sysinfo_;		/* Have we gotten system info yet? */
	Bool have_userinfo_;		/* Have we gotten user info yet? */
	Bool have_netsyncinfo_;		/* Have we gotten NetSync info yet? */
	struct dlp_sysinfo sysinfo_;	/* System information */
	struct dlp_userinfo userinfo_;	/* User information */
	struct dlp_netsyncinfo netsyncinfo_;
					/* NetSync information */

	char serial_[SNUM_MAX];		/* Serial number */
	signed char serial_len_;	/* Length of serial number */

	/* Memory information */
	int num_cards_;			/* # memory cards */
	struct dlp_cardinfo *cardinfo_;	/* Info about each memory card */

	/* Database information */
	/* XXX - There should probably be one array of these per memory
	 * card. Or perhaps 'struct dlp_dbinfo' should say which memory
	 * card it is on.
	 */
	Bool have_all_DBs_;		/* Do we have the entire list of
					 * databases? */
	int num_dbs_;			/* # of databases */
	struct dlp_dbinfo *dblist_;	/* Database list */
	int dbit_;			/* Iterator for palm_nextdb() */
};

/* Constructor, destructor */
extern struct Palm *new_Palm(PConnection *pconn);
extern void free_Palm(struct Palm *palm);

/* Accessors. One for each datum one might want to get */
extern const udword palm_rom_version(struct Palm *palm);
extern const int palm_serial_len(struct Palm *palm);
extern const char *palm_serial(struct Palm *palm);
extern const int palm_num_cards(struct Palm *palm);

extern const char *palm_username(struct Palm *palm);
extern const udword palm_userid(struct Palm *palm);
extern const udword palm_viewerid(struct Palm *palm);
extern const udword palm_lastsyncPC(struct Palm *palm);
/* XXX - lastgoodsync */
/* XXX - lastsync */
/* XXX - passwd */

/* XXX - This needs to be redone as a whole set of accessors */
/*  extern int ListDBs(PConnection *pconn, struct Palm *palm); */
extern int palm_fetch_all_DBs(struct Palm *palm);
extern const int palm_num_dbs(struct Palm *palm);
extern void palm_resetdb(struct Palm *palm);
extern const struct dlp_dbinfo *palm_nextdb(struct Palm *palm);
extern const struct dlp_dbinfo *palm_find_dbentry(struct Palm *palm,
						  const char *name);
extern int palm_append_dbentry(struct Palm *palm,
			       struct pdb *pdb);
extern const char *palm_netsync_hostname(struct Palm *palm);
extern const char *palm_netsync_hostaddr(struct Palm *palm);
extern const char *palm_netsync_netmask(struct Palm *palm);

#endif	/* _spalm_h_ */

/* This is for Emacs's benefit:
 * Local Variables:	***
 * fill-column:	75	***
 * End:			***
 */
