/* symboltable.h
 *
 * A symbol table for use in the parser.
 *
 *	Copyright (C) 2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: symboltable.h,v 2.3 2001-10-18 01:39:10 arensb Exp $
 */

#ifdef __cplusplus
extern "C" {
#endif	/* __cplusplus */
extern char *get_symbol(const char *name);
			/* Get a symbol from the table that matches
			 * the given key. */
extern char *get_symbol_n(const char *name, int len);
			/* Get a symbol from the table that matches
			 * the given key. The key has length len, and
			 * does not have to be NUL-terminated. */
extern void put_symbol(const char *name, const char *value);
			/* Put a symbol onto the table. (name is the
			 * key, value is the value.) */
extern void symboltable_init(void);
			/* Initialize the symbol table based on the
			 * arguments. */
#ifdef __cplusplus
};
#endif	/* __cplusplus */

/* This is for Emacs's benefit:
 * Local Variables:	***
 * fill-column:	75	***
 * End:			***
 */
