/* symboltable.cc
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: symboltable.cc,v 1.1.2.2 2001-10-11 03:19:48 arensb Exp $
 */

#include <config.h>

#include <string>
#include <map>
#include <stdlib.h>

extern "C" {
#include "symboltable.h"
}

map<string,string> table;	/* XXX - Is this going to cause problems on
				 * machines where the assembler or linker
				 * can't handle identifiers of length 4096?
				 */

/* Get a symbol from the table that matches the give key.   */
string get_symbol(const string &key) {
	string value = table[key];
	if( value == "") {
		char *env = getenv(key.c_str());
		if( env != NULL ) value = env;
	}
	return value;
}

/* This is a helper function to convert to  C from C++ */
char *make_c_string(const string &s) {
	char *ret = (char *)malloc(s.length() + 1);
	s.copy(ret, s.length());
	ret[s.length()] = 0;
	return ret;
}


/* Get a symbol from the table that matches the give key. */
char *get_symbol(const char *name) {
	string key(name);
	return make_c_string(get_symbol(key));
}

/* Get a symbol from the table that matches the give key.  The key has
 * length len, and does not have to be null terminated.
 */
char *get_symbol_n(const char *name, int len) {
	if(len < 0 ) len = strlen(name);
	string key(name, len);
	return make_c_string(get_symbol(key));
}

/* Puy a symbol onto the table. (name is the key, value is the value. )
 */
void put_symbol(const char *name, const char *value) {
	table[name] = value;
}

/* Initialize the symbol table based on the arguments.
 */
void symboltable_init() {
	char * dir = getenv("CONDUITDIR");
	if( dir != 0 ) {
		table["CONDUITDIR"] = dir;
	} else {
		table["CONDUITDIR"] = CONDUITDIR; // defined in ../Make.rules
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
