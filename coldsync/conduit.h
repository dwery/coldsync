/* conduit.h
 *
 * Definitions, declarations pertaining to conduits.
 *
 * $Id: conduit.h,v 1.1 1999-06-24 02:50:44 arensb Exp $
 */
#ifndef _conduit_h_
#define _conduit_h_

#include "palm/palm_types.h"
#include "pconn/dlp_cmd.h"
#include "coldsync.h"

#define COND_NAMELEN		128	/* Max. length of conduit name */

typedef int (*ConduitFunc)(struct PConnection *pconn,
			   struct Palm *palm,
			   struct dlp_dbinfo *db);

/* conduit_spec
 * Describes a conduit.
 */
struct conduit_spec
{
	char name[COND_NAMELEN];	/* Name of this conduit */

	/* What databases does this conduit apply to? */
	char dbname[DLPCMD_DBNAME_LEN];
			/* Name of the database that the conduit
			 * applies to. Empty string means conduit
			 * applies to all databases */
	udword dbtype;	/* Database type. 0 means conduit applies to
			 * all types. */
	udword dbcreator;
			/* Database creator. 0 means conduit applies
			 * to all creators. */
	/* XXX - Need an enum to specify whether this is a regular
	 * conduit, a pre-fetch function, or a post-dump function.
	 */

	/* The actual functions involved */
	/* XXX - Should there be some mechanism to indicate that this
	 * conduit may be used in conjunction with others? I.e., there
	 * might be one post-dump function that reads the address book
	 * and converts it to a MH mail alias list, another that
	 * converts it to a Pine alias list. Both should be run.
	 */
/*  	int (*run)(struct Palm *palm, struct dlp_dbinfo *db); */
	ConduitFunc run;
			/* The conduit function. */
};

/* conduit_config
 * Configuration for a given conduit: specifies when it should be run
 * (which databases and whatnot).
 */
struct conduit_config
{
	/* Database characteristics
	 * If 'dbname' is the empty string, it's a wildcard: this
	 * conduit is applicable to all databases. If 'dbtype' or
	 * 'dbcreator' is 0, then it's a wildcard: this conduit
	 * applies to all types and creators.
	 * When a conduit is registered, 'dbname', 'dbtype' and
	 * 'dbcreator' are and-ed with their corresponding values in
	 * '*conduit'. Thus, if a conduit applies to (*, 'appl', *)
	 * and is registered with ("Memo Pad", *, *), then it will
	 * only be invoked for ("Memo Pad", 'appl', *).
	 */
	char dbname[DLPCMD_DBNAME_LEN];	/* Database name */
	udword dbtype;			/* Database type */
	udword dbcreator;		/* Database creator */

	/* Other information */
	Bool mandatory;			/* If true, stop looking if
					 * this conduit matches:
					 * prevents other conduits
					 * from overriding this one.
					 */

	/* Information needed at run-time */
	struct conduit_spec *conduit;	/* The conduit itself. If this
					 * is NULL, then don't do
					 * anything when syncing this
					 * database.
					 */
	/* XXX - Arguments and such (e.g., for commands or scripts) */
};

extern int init_conduits(struct Palm *palm);
					/* Initialize global conduit stuff */
extern int tini_conduits();		/* Clean up global conduit stuff */
extern int load_conduit(struct conduit_spec *spec);
extern int unload_conduit(const char *name);
extern struct conduit_spec *getconduitbyname(const char *name);
/*  extern int register_conduit(const struct conduit_config *config); */
extern int register_conduit(const char *name,
			    const char *dbname,
			    const udword dbtype,
			    const udword dbcreator,
			    const Bool mandatory);
/*  extern int unregister_conduit(const char *name); */ /* XXX */
extern const struct conduit_spec *find_conduit(const struct dlp_dbinfo *db);

/* XXX

 * - Add conduit to the pool of available conduits (load_conduit())
 *	A conduit is only applicable to a certain set of databases,
 *	e.g. ("MemoDB", *, *) or (*, 'appl', *). This is specified by
 *	load_conduit().
 * - Remove conduit from the pool of available conduits (unload_conduit())
 * - Add a conduit to the list of conduits that will be used
 * (register_conduit())
 *	This may also specify a subset of databases, similar to the
 *	way that load_conduit() does. The two are and-ed together: if
 *	load_conduit() specifies ("MemoDB", *, *) and
 *	register_conduit() specifies (*, 'appl', *), the conduit will
 *	only be used for ("MemoDB", 'appl', *). If a field conflicts,
 *	this is an error: ("foo", *, *) + ("bar", *, *) -> error.
 *	(Should it be possible to override this?)
 *	There should also be some way to specify that this conduit
 *	only applies to a certain category within the database.
 */

/* XXX - Need conduits:
 * - "Generic": just sync using the logic of the pigeon book.
 * - "Copy from Palm": the Palm is authoritative.
 * - "Copy from Desktop": the desktop is authoritative.
 * - "Ignore": don't do anything with this database.
 * - "cmd": execute a Bourne shell command
 * - "perl": execute or load a Perl script
 * - "python": ditto, Python
 * - "tcl": ditto, Tcl
 * - etc.
 */

/* XXX - Need API for dynamically-loaded files:

 * - Initialization:
 *	If array 'conduit_list' exists, call load_conduit() on each
 *	element.
 *	Otherwise, if 'init_conduits()' exists, call it.
 * - Cleanup:
 *	???
 */

#endif	/* _conduit_h_ */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
