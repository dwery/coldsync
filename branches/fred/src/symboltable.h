/* symboltable.h
 *
 * A symbol table for use in the parser.
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: symboltable.h,v 1.1.2.1 2001-10-09 01:41:28 arensb Exp $
 */


/* Get a symbol from the table that matches the give key.  
 */
extern char *get_symbol(const char *name);
/* Get a symbol from the table that matches the give key.  The key has
 * length len, and does not have to be null terminated.
 */
extern char *get_symbol_n(const char *name, int len);
/* Puy a symbol onto the table. (name is the key, value is the value. )
 */
extern void put_symbol(const char *name, const char *value);

/* Initialize the symbol table based on the arguments. */
extern void symboltable_init();
