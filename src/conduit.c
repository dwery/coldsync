/* conduit.c
 *
 * Functions for dealing with conduits: looking them up, loading
 * libraries, invoking them, etc.
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: conduit.c,v 1.7 1999-11-10 09:07:20 arensb Exp $
 */
/* XXX - At some point, the API for built-in conduits should become much
 * simpler. A lot of the crap in this file will disappear, since it's
 * intended to handle an obsolete conduit model. Presumably, there'll just
 * be a single table that maps an internal conduit name (e.g., "<Generic>")
 * to the function that implements it.
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include "conduit.h"

extern int run_GenericConduit(struct PConnection *pconn,
			      struct Palm *palm,
			      struct dlp_dbinfo *db);
struct conduit_spec *getconduitbyname(const char *name);
int cond_MemoDB(struct PConnection *pconn,
		struct Palm *palm,
		struct dlp_dbinfo *db);

/* XXX - This static array is bogus: it should be dynamically grown.
 * In fact, this should probably be a hash table, keyed by conduit
 * name.
 */
static struct conduit_spec *cond_pool[128];
				/* Pool of all available conduits */
/* XXX - This will be used later. Currently turned off to keep gcc
 * from fussing.
 */
/*static int cond_poolsize = sizeof(cond_pool) / sizeof(cond_pool[0]);*/
				/* Size of conduit pool */
static int num_conduits = 0;	/* Total number of conduits available */

/* XXX - This static array is bogus: it should be dynamically grown.
 */
static struct conduit_config cond_list[128];
				/* List of registered conduits: each
				 * database to be synced will be
				 * looked up in this array.
				 */
static int cond_list_len = 0;	/* # elements in cond_list */

/* Initial values for conduit pool */
static struct conduit_spec init_conduit_list[] = {
/*  	{ "Generic", "", 0L, 0L, cond_generic, }, */
	{ "Generic C++", "", 0L, 0L, run_GenericConduit, },
	{ "Memo", "MemoDB", 0L, 0L, cond_MemoDB, },
};
static const int init_conduit_list_len = 
	sizeof(init_conduit_list) / sizeof(init_conduit_list[0]);

/* init_conduits
 * Initialize everything conduit-related.
 */
int
init_conduits(struct Palm *palm)
{
	int i;

	/* XXX */
	for (i = 0; i < init_conduit_list_len; i++)
		load_conduit(&init_conduit_list[i]);
/*  	register_conduit("Generic", NULL, 0L, 0L, False); */
/*  	register_conduit("Memo", "MemoDB", 0L, 0L, True); */
	register_conduit("Generic C++", NULL, 0L, 0L, False);
	return 0;
}

/* tini_conduits
 * Clean up everything conduit-related.
 */
int
tini_conduits()
{
	/* XXX */
	return 0;
}

/* load_conduit
 * Add a conduit to the pool.
 */
int
load_conduit(struct conduit_spec *spec)
{
	/* XXX - This will need to be redone when cond_pool is
	 * implemented properly.
	 */
	cond_pool[num_conduits] = spec;
	num_conduits++;

	return 0;
}

/* unload_conduit
 * Remove a conduit from the pool.
 * XXX - Is this really necessary?
 * XXX - The conduit will also need to be removed from cond_list (as
 * well as the pre-fetch and post-dump lists, if those are used).
 */
int
unload_conduit(const char *name)
{
	/* XXX */
	return 0;
}

struct conduit_spec *
getconduitbyname(const char *name)
{
	int i;

	if (name == NULL)
		return NULL;

/*  fprintf(stderr, "getconduitbyname: num_conduits == %d\n", num_conduits); */
	for (i = 0; i < num_conduits; i++)
	{
/*  fprintf(stderr, "getconduitbyname: checking %d\n", i); */
		if (strcmp(name, cond_pool[i]->name) == 0)
		{
/*  fprintf(stderr, "getconduitbyname(\"%s\"): found %d\n", name, i); */
			/* Found it */
			return cond_pool[i];
		}
	}

	return NULL;		/* Couldn't find it */
}

/* register_conduit
 * Add a conduit to the end of the list of active conduits: when a
 * database is synced, this conduit will be checked and run, if
 * appropriate.
 * Returns 0 if successful, or -1 if the conduit could not be found,
 * or some other error occurred.
 * XXX - It should be possible to specify that this conduit should
 * only be used for certain databases.
 */
int
register_conduit(const char *name,
		 const char *dbname,
		 const udword dbtype,
		 const udword dbcreator,
		 const Bool mandatory)
{
	struct conduit_spec *spec;

	spec = getconduitbyname(name);

	if (spec == NULL)
	{
		/* No conduit defined, so don't do anything to
		 * sync this database. Also, this is a special
		 * case since we can't make sure that the
		 * conduit is applicable to this database
		 * (since there _is_ no conduit).
		 */
		if (dbname == NULL)
			cond_list[cond_list_len].dbname[0] = '\0';
		else
			strncpy(cond_list[cond_list_len].dbname, dbname,
				DLPCMD_DBNAME_LEN);
		cond_list[cond_list_len].dbtype = dbtype;
		cond_list[cond_list_len].dbcreator = dbcreator;
		cond_list[cond_list_len].mandatory = mandatory;
		cond_list[cond_list_len].conduit = NULL;
		cond_list_len++;

		return 0;
	}

	/* Make sure the configuration doesn't conflict with the
	 * applicability of the conduit, as defined in its spec.
	 */

	/* Check database name */
	if ((dbname != NULL) &&
	    (dbname[0] != '\0') &&
	    (spec->dbname[0] != '\0') &&
	    (strcmp(dbname, spec->dbname) != 0))
	{
		/* Oops! Both the conduit spec and the arguments
		 * specify a database name, and they're not the same.
		 * This is an error.
		 */
		SYNC_TRACE(3)
			fprintf(stderr, "register_conduit: %s: "
				"Database name conflict: called with \"%s\", "
				"but spec says \"%s\"\n",
				spec->name,
				dbname, spec->dbname);
		return -1;
	}

	/* At most one database name was specified. Use it */
	if (spec->dbname[0] != '\0')
		strncpy(cond_list[cond_list_len].dbname, spec->dbname,
			DLPCMD_DBNAME_LEN);
	else if ((dbname != NULL) && (dbname[0] != '\0'))
		strncpy(cond_list[cond_list_len].dbname, dbname,
			DLPCMD_DBNAME_LEN);
	else
		cond_list[cond_list_len].dbname[0] = '\0';

	/* Check the database type */
	if ((dbtype != 0L) &&
	    (spec->dbtype != 0L) &&
	    (dbtype != spec->dbtype))
	{
		/* Oops! Both the conduit spec and 'config'
		 * specify a database type, and they're not
		 * the same. This is an error.
		 */
		SYNC_TRACE(3)
			fprintf(stderr, "register_conduit: %s: "
				"Database type conflict: config says 0x%08lx, "
				"but spec says 0x%08lx\n",
				spec->name,
				dbtype, spec->dbtype);
		return -1; 
	}

	/* At most one database type was specified. Use it */
	if (dbtype != 0L)
		cond_list[cond_list_len].dbtype = dbtype;
	else
		cond_list[cond_list_len].dbtype = spec->dbtype;

	/* Check the database creator */
	if ((dbcreator != 0L) &&
	    (spec->dbcreator != 0L) &&
	    (dbcreator != spec->dbcreator))
	{
		/* Oops! Both the conduit spec and 'config'
		 * specify a database creator, and they're not
		 * the same. This is an error.
		 */
		SYNC_TRACE(3)
			fprintf(stderr, "register_conduit: %s: "
				"Database creator conflict: config says "
				"0x%08lx, but spec says 0x%08lx\n",
				spec->name,
				dbcreator, spec->dbcreator);
		return -1;
	}

	/* At most one database creator was specified. Use it */
	if (dbcreator != 0L)
		cond_list[cond_list_len].dbcreator = dbcreator;
	else
		cond_list[cond_list_len].dbcreator = spec->dbcreator;

	/* Record other pertinent information */
	cond_list[cond_list_len].mandatory = mandatory;
	cond_list[cond_list_len].conduit = spec;

	SYNC_TRACE(2)
		fprintf(stderr, "Registered conduit \"%s\" "
			"for database \"%s\", type 0x%08lx, creator 0x%08lx\n",
			cond_list[cond_list_len].conduit->name,
			cond_list[cond_list_len].conduit->dbname,
			cond_list[cond_list_len].conduit->dbtype,
			cond_list[cond_list_len].conduit->dbcreator);

	cond_list_len++;	/* XXX - Potential overflow */
	return 0;
}

int
unregister_conduit(const char *name)
{
	/* XXX */
	return 0;
}

/* find_conduit
 * Find the conduit to run for the database 'db'. Returns a pointer to
 * the conduit spec, or NULL if no appropriate conduit was found.
 */
const struct conduit_spec *
find_conduit(const struct dlp_dbinfo *db)
{
	int i;
	struct conduit_spec *retval;

/*  fprintf(stderr, "find_conduit: looking for \"%s\"\n", db->name); */
	/* Walk the list of registered conduits */
	retval = NULL;
	for (i = 0; i < cond_list_len; i++)
	{
/*  fprintf(stderr, "Examining \"%s\" 0x%08lx/0x%08lx\n",
	cond_list[i].dbname, cond_list[i].dbtype, cond_list[i].dbcreator);*/
		if ((cond_list[i].dbname[0] != '\0') &&
		    (strncmp(db->name, cond_list[i].dbname,
			     DLPCMD_DBNAME_LEN) != 0))
			/* Database names don't match */
			continue;

		if ((cond_list[i].dbtype != 0L) &&
		    (db->type != cond_list[i].dbtype))
			/* Database types don't match */
			continue;

		if ((cond_list[i].dbcreator != 0L) &&
		    (db->creator != cond_list[i].dbcreator))
			/* Database creators don't match */
			continue;

		/* Found a match */
/*  fprintf(stderr, "Found a match: i == %d\n", i); */
		retval = cond_list[i].conduit;

		if (cond_list[i].mandatory)
			/* This conduit is mandatory. Stop looking */
			return retval;
	}
/*  fprintf(stderr, "Returning %dth conduit: 0x%08lx\n", i, (long) retval); */
	return retval;		/* Return the last conduit found, if
				 * any */
}

/* XXX - Very experimental */
int
cond_MemoDB(struct PConnection *pconn,
	    struct Palm *palm,
	    struct dlp_dbinfo *db)
{
	fprintf(stderr, "Inside cond_MemoDB()\n");
	return 0;
}

/* run_Fetch_conduits
 * Go through the list of Fetch conduits and run whichever ones are
 * applicable for the database 'dbinfo'.
 */
int
run_Fetch_conduits(struct Palm *palm,
		   struct dlp_dbinfo *dbinfo)
{
	conduit_block *conduit;

	SYNC_TRACE(2)
		fprintf(stderr, "Running pre-fetch conduits for \"%s\".\n",
			dbinfo->name);

	for (conduit = config.fetch_q;
	     conduit != NULL;
	     conduit = conduit->next)
	{
		SYNC_TRACE(3)
			fprintf(stderr, "Trying conduit...\n");

		/* See if the creator matches */
		if ((conduit->dbcreator != 0) &&
		    (conduit->dbcreator != dbinfo->creator))
		{
			SYNC_TRACE(5)
				fprintf(stderr,
					"  Creator: database is 0x%08lx, "
					"conduit is 0x%08lx. Not applicable.\n",
					dbinfo->creator,
					conduit->dbcreator);
			continue;
		}

		/* See if the creator matches */
		if ((conduit->dbtype != 0) &&
		    (conduit->dbtype != dbinfo->type))
		{
			SYNC_TRACE(5)
				fprintf(stderr, "  Type: database is 0x%08lx, "
					"conduit is 0x%08lx. Not applicable.\n",
					dbinfo->type,
					conduit->dbtype);
			continue;
		}

		/* This conduit matches */
		SYNC_TRACE(2)
			fprintf(stderr, "  This conduit matches. "
				"I ought to run \"%s\"\n",
				(conduit->path == NULL ? "(null)" :
				 conduit->path));
	}

	return 0;		/* XXX */
}

/* run_Dump_conduits
 * Go through the list of Dump conduits and run whichever ones are
 * applicable for the database 'dbinfo'.
 */
int
run_Dump_conduits(struct Palm *palm,
		  struct dlp_dbinfo *dbinfo)
{
	conduit_block *conduit;

	SYNC_TRACE(2)
		fprintf(stderr, "Running post-dump conduits for \"%s\".\n",
			dbinfo->name);

	for (conduit = config.dump_q;
	     conduit != NULL;
	     conduit = conduit->next)
	{
		SYNC_TRACE(3)
		fprintf(stderr, "Trying conduit...\n");

		/* See if the creator matches */
		if ((conduit->dbcreator != 0) &&
		    (conduit->dbcreator != dbinfo->creator))
		{
			SYNC_TRACE(5)
				fprintf(stderr, "  Creator: database is 0x%08lx, "
					"conduit is 0x%08lx. Not applicable.\n",
					dbinfo->creator,
					conduit->dbcreator);
			continue;
		}

		/* See if the creator matches */
		if ((conduit->dbtype != 0) &&
		    (conduit->dbtype != dbinfo->type))
		{
			SYNC_TRACE(5)
				fprintf(stderr, "  Type: database is 0x%08lx, "
					"conduit is 0x%08lx. Not applicable.\n",
					dbinfo->type,
					conduit->dbtype);
			continue;
		}

		/* This conduit matches */
		SYNC_TRACE(2)
			fprintf(stderr, "  This conduit matches. "
				"I ought to run \"%s\"\n",
				(conduit->path == NULL ? "(null)" :
				 conduit->path));
	}

	return 0;		/* XXX */
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
