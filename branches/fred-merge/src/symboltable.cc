/* symboltable.cc
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: symboltable.cc,v 1.1.4.1 2001-10-11 03:38:03 arensb Exp $
 */

#include <config.h>

#include <string>
#include <map>
#include <stdlib.h>

extern "C" {
#include <stdlib.h>		/* For malloc() */
#include "symboltable.h"
}

map<string,string> table;	/* XXX - Is this going to cause problems on
				 * machines where the assembler or linker
				 * can't handle infinitely-long
				 * identifiers?
				 */
	/* XXX - This needs work. I guess it should really be
	 *	map <string,SymTabEntry> table;
	 * for some appropriate type SymTabEntry.
	 * In particular, it should be possible to mark a value as 'final',
	 * so that it may not be modified in the future. This would allow
	 * the sysadmin to mandate certain variables' values ($CONDUITDIR,
	 * perhaps?)
	 */
/* XXX - In these functions, it it worth checking to see whether the key is
 * the empty string?
 */

/* get_symbol
 * Fetch the symbol 'key' from the symbol table. If not found, see if the
 * key is an environment variable, and use that.
 * Returns the key's value, or NULL if not found.
 */
/* XXX - Does this allocate a string for the return value? If so, does this
 * leak memory?
 */
string
get_symbol(const string &key)
{
	string value = table[key];

	if (value == "")
	{
		/* No such entry in the symbol table. Look in the
		 * environment.
		 */
		char *env = getenv(key.c_str());

		if (env != NULL)
			value = env;
	}
	return value;
}

/* Get a symbol from the table that matches the given key. */
/* XXX - Should be inline */
char *
get_symbol(const char *name)
{
	string key(name);
	return make_c_string(get_symbol(key));
}

/* make_c_string
 * C helper function: convert an STL string to a C-style "char *".
 */
char *
make_c_string(const string &s)
{
	char *ret = (char *) malloc(s.length() + 1);
			/* XXX - Error-checking: make sure the malloc()
			 * succeeded.
			 */

	s.copy(ret, s.length());
	ret[s.length()] = 0;
	return ret;
}

/* Get a symbol from the table that matches the given key. The key has
 * length len, and does not have to be null terminated.
 */
char *
get_symbol_n(const char *name, int len)
{
	if (len < 0)
		len = strlen(name);

	string key(name, len);
	return make_c_string(get_symbol(key));
}

/* Put a symbol onto the table. (name is the key, value is the value.)
 */
// XXX - Rename this to insert_symbol()? "put" is kinda vague.
void
put_symbol(const char *name, const char *value)
{
	table[name] = value;
}

/* Initialize the symbol table based on the arguments.
 */
// XXX - Rename as "init_symboltable()", for style consistency
void symboltable_init(void)
{
	char * dir = getenv("CONDUITDIR");

	if (dir != 0) {
		table["CONDUITDIR"] = dir;
	} else {
		table["CONDUITDIR"] = CONDUITDIR; // defined in "config.h"
	}
}


/*
  XXX One problem with the symbol table as it stands:
  The comand line arguments need to be parsed first, so that the config file
  can be named using -f <file>.

  However, some variables that are defined on the command line should override
  variables defined in the config file, such as logfile.

  However, some variables, can be overriden, or at least expanded in the config
  file, such as CONDUITDIR = "$(CONDUITDIR):usr/local/other/dir".

  Two choices to fix this:
  -- mark some variables as fixed when they are set by args, so that they cannot
     be set again.
  -- reprocess the command line args after the config file is read. 

*/
   

/* This is for Emacs's benefit:
 * Local Variables:	***
 * fill-column:	75	***
 * End:			***
 */
