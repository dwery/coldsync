/* symboltable.cc
 *
 *	Copyright (C) 2002, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: symboltable.c,v 1.6 2003-06-26 20:54:36 azummo Exp $
 *
 * This file implements the symbol table, as used in the .coldsyncrc
 * parser. No, it's not terribly sophisticated, because we're likely
 * not going to be dealing with huge numbers of symbols.
 *
 * "Rule 3.  Fancy algorithms are slow when n is small, and n is
 * usually small." -- Rob Pike, "Notes on Programming in C"
 */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>		/* For malloc() */
#include <string.h>		/* For strlen(), strncpy(), etc. */
#include "coldsync.h"
#include "symboltable.h"

struct symbol
{
	struct symbol *next;	/* Next entry in list */
	char *key;		/* Symbol name */
	char *value;		/* Symbol value */
	/* XXX - Perhaps add flags: "final", "default", etc. */
};

static struct symbol *symtab = NULL;	/* The symbol table */

static struct symbol *new_symbol(const char *key, const char *value);
static void free_symbol(struct symbol *sym);

void
symboltable_init(void)
{
	return;			/* Nothing to do, really */
}

void
symboltable_tini(void)
{
	struct symbol *sym;

	/* Free everything */
	for (sym = symtab; sym != NULL; sym = sym->next)
		free_symbol(sym);
}

/* new_symbol
 * Allocate a new 'struct symbol', with the given key and value. 'key'
 * and 'value' are copied to the newly-created symbol.
 */
struct symbol *
new_symbol(const char *key, const char *value)
{
	struct symbol *retval;
	int len;

	MISC_TRACE(7)
		fprintf(stderr, "Inside new_symbol(%s,%s)\n", key, value);

	/* Allocate the new symbol */
	if ((retval = (struct symbol *) malloc(sizeof(struct symbol))) == NULL)
		return NULL;		/* Out of memory */

	retval->next = NULL;

	if (key == NULL)
		retval->key = NULL;
	else {
		/* Allocate space for the key (and terminating NUL) */
		len = strlen(key);
		if ((retval->key = (char *) malloc(len+1)) == NULL)
		{
			/* Out of memory */
			free(retval);
			return NULL;
		}
		strncpy(retval->key, key, len+1);
	}

	if (value == NULL)
		retval->value = NULL;
	else {
		/* Allocate space for the value */
		len = strlen(value);
		if ((retval->value = (char *) malloc(len+1)) == NULL)
		{
			/* Out of memory */
			free(retval->key);
			free(retval);
			return NULL;
		}
		strncpy(retval->value, value, len+1);
	}

	MISC_TRACE(7)
		fprintf(stderr, "\treturning %p\n", (void *)retval);
	return retval;
}

/* free_symbol
 * Free a 'struct symbol'.
 */
void
free_symbol(struct symbol *sym)
{
	MISC_TRACE(7)
		fprintf(stderr, "Inside free_symbol(%p == %s)\n",
			(void *) sym, (sym == NULL ? "(nil)" : sym->key));

	if (sym == NULL)
		return;
	if (sym->key != NULL)
		free(sym->key);
	if (sym->value != NULL)
		free(sym->value);
	return;
}

/* get_symbol
 * Find the symbol with name 'name' in the symbol table, copy its
 * value, and return a pointer to this copy. Returns NULL if the
 * symbol was not found.
 */
char *
get_symbol(const char *name)
{
	struct symbol *sym;
	char *env;		/* Environment variable */

	MISC_TRACE(6)
		fprintf(stderr, "Inside get_symbol(%s)\n", name);

	for (sym = symtab; sym != NULL; sym = sym->next)
	{
		MISC_TRACE(7)
			fprintf(stderr, "\tChecking [%s]\n", sym->key);
		if (strcmp(sym->key, name) == 0)
		{
			MISC_TRACE(6)
				fprintf(stderr, "\tReturning [%s]\n",
					sym->value);

			/* Found it */
			return strdup(sym->value);
		}
	}

	/* Couldn't find it in the symbol table. Look in the environment */
	MISC_TRACE(6)
		fprintf(stderr,
			"\tNot found in symtab. Looking in environment\n");
	env = getenv(name);
	MISC_TRACE(6)
		fprintf(stderr, "\tenv == [%s]\n",
			(env == NULL ? "(nil)" : env));

	if (env == NULL)
		return NULL;		/* Couldn't find it anywhere */
	else
		return strdup(env);
}

/* put_symbol
 * Add or replace a symbol in the table. If a symbol with the given
 * name already exists, replace it. Otherwise, append it to the list.
 */
int
put_symbol(const char *name, const char *value)
{
	struct symbol *oldsym = NULL;	/* Existing symbol with same name */
	struct symbol *last = NULL;	/* Last symbol on the list */
	struct symbol *newsym;		/* Symbol to be added */

	MISC_TRACE(6)
	{
		fprintf(stderr, "Inside put_symbol(%s,%s)\n", name, value);
		fprintf(stderr, "\tsymtab == %p\n", (void *) symtab);
	}

	/* Automatically set an env var with the same name */
	setenv(name, value, 1);

	/* Create a new symbol */
	if ((newsym = new_symbol(name, value)) == NULL)
		return -1;

	/* See if there's already a symbol with name 'name' */
	for (oldsym = symtab; oldsym != NULL; oldsym = oldsym->next)
	{
		MISC_TRACE(7)
			fprintf(stderr, "\toldsym == %p; last == %p\n",
				(void *) oldsym,
				(void *) last);

		if (strcmp(oldsym->key, name) == 0)
			/* Found it */
			break;
		last = oldsym;		/* Keep track of end of list */
	}
	MISC_TRACE(7)
		fprintf(stderr, "\tlast: oldsym == %p; last == %p\n",
			(void *) oldsym,
			(void *) last);

	/* Add the new symbol to the list */
	if (oldsym == NULL)
	{
		/* This is a new symbol. Append it to the list. */
		if (last == NULL)
			/* Just starting the list */
			symtab = newsym;
				/* (last == NULL) implies (symtab == NULL),
				 * right? Right? Otherwise, this'll blow
				 * away 'symtab' and cause a memory leak.
				 */
		else
			/* Append to existing list */
			last->next = newsym;
			
	} else {
		/* Replace existing symbol */
	
		newsym->next = oldsym->next;

		if (last != NULL)
		{
			last->next = newsym;
		}
		else
		{
			/* We are replacing the head */
			symtab = newsym;
		}

		free_symbol(oldsym);
	}

	return 0;
}

/* This is for Emacs's benefit:
 * Local Variables:	***
 * fill-column:	75	***
 * End:			***
 */
