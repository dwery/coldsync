/* symboltable.h
 *
 * A symbol table for use in the parser.
 *
 *	Copyright (C) 2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: symboltable.h,v 1.1.4.2 2001-10-12 02:35:39 arensb Exp $
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
extern void symboltable_init();
			/* Initialize the symbol table based on the
			 * arguments. */
#ifdef __cplusplus
};
#endif	/* __cplusplus */
