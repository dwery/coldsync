/* symboltable.h
 *
 * A symbol table for use in the parser.
 *
 *	Copyright (C) 2001-2002, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: symboltable.h,v 2.4 2002-03-18 08:28:41 arensb Exp $
 */
/* XXX - Redo the API: get_symbol() ought to return a (struct symbol *). */

#ifdef __cplusplus
extern "C" {
#endif	/* __cplusplus */
extern char *get_symbol(const char *name);
			/* Get a symbol from the table that matches
			 * the given key. */
			/* XXX - This looks in the environment by default.
			 * But "config.c" uses symbol "LOGFILE", which
			 * seems a bit too generic. So perhaps there ought
			 * to be a separate get_symbol_env() which looks in
			 * the environment. Perhaps it should take a
			 * separate argument giving the name of the
			 * environment variable to look up.
			 */
extern int put_symbol(const char *name, const char *value);
			/* Put a symbol onto the table. (name is the
			 * key, value is the value.) */
extern void symboltable_init(void);
			/* Initialize the symbol table */
extern void symboltable_tini(void);
			/* Clean up the symbol table */
#ifdef __cplusplus
};
#endif	/* __cplusplus */

/* This is for Emacs's benefit:
 * Local Variables:	***
 * fill-column:	75	***
 * End:			***
 */
