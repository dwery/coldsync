/* conduit.h
 *
 * Definitions, declarations pertaining to conduits.
 *
 *	Copyright (C) 1999, 2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: conduit.h,v 1.14 2000-11-18 22:44:24 arensb Exp $
 */
#ifndef _conduit_h_
#define _conduit_h_

/* XXX - There are two aspects to conduit configuration: `struct
 * conduit_desc' is supplied by the conduit (i.e., by the person writing
 * the conduit) describes what a conduit _can_ do; `struct conduit_config'
 * is supplied by the ColdSync config file(s) and specifies what a conduit
 * _should_ do.
 *
 * At initialization, find out what conduits are available. Put all of
 * these conduits (their `conduit_desc', that is) into a pool. Then read
 * the config files and create "should" lists out of them. These lists
 * specify which conduits ought to do what, and in what order to search for
 * them.
 *
 * It's probably best to have one "should" list per function: that is, have
 * one list for pre-fetch conduits, one for syncing, one for installs, and
 * so forth. This should allow maximal flexibility: say you have two
 * conduits, "Freshmeat" and "NewsDump" that both provide `sync' and
 * `post-dump' functions for the same set of databases. The scheme outlined
 * above allows you to use "Freshmeat"'s `sync' function and "NewsDump"'s
 * `post-dump' function on the same database.
 *
 * `struct conduit_desc' contains the name of the conduit (possibly its
 * version),
 */

#include "config.h"
#include "pconn/pconn.h"
#include "coldsync.h"
#include "parser.h"

#define COND_MAXHFIELDLEN	31	/* Max allowable length of a header
					 * field label. That is, when
					 * sending the header
					 *    Foo: bar baz
					 * the "Foo" part may not be longer
					 * than this.
					 */
#define COND_MAXLINELEN		255	/* Max allowable length of a header
					 * line, including the label. That
					 * is, when sending the header
					 *    Foo: bar baz
					 * that entire line may not be
					 * longer than COND_MAXLINELEN.
					 */
/* Sanity check: the max length of a line should be at least long enough to
 * hold the longest field label, ": ", and one character of data.
 */
#if COND_MAXLINELEN < COND_MAXHFIELDLEN + 3
#  error COND_MAXLINELEN is too small!
#endif

extern int run_Fetch_conduits(const struct dlp_dbinfo *dbinfo);
extern int run_Dump_conduits(const struct dlp_dbinfo *dbinfo);
extern int run_Sync_conduits(const struct dlp_dbinfo *dbinfo,
			     struct PConnection *pconn);
extern int run_Install_conduits(struct dlp_dbinfo *dbinfo);

#endif	/* _conduit_h_ */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
