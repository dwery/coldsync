%{ /* -*- C -*- */
/* parser.y
 * Config file parser.
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: parser.y,v 2.1 1999-10-18 09:15:56 arensb Exp $
 */
#include "config.h"
#include <stdio.h>

extern int yyerror(char *msg);
extern int yylex(void);
%}

%token <integer>	NUMBER
%token <string>		STRING
%token ARGS
%token CONDUIT
%token LISTEN
%token PORT
%token SERIAL
%token SPEED

%union {
	int integer;
	char *string;
}

%%
file:	statements
	{ fprintf(stderr, "Found a file\n"); }
	;

statements:	statements statement
	{ fprintf(stderr, "Found a statement\n"); }
	|	/* Empty */
	{ fprintf(stderr, "Found an empty statement\n"); }
	;

statement:
	listen_stmt
	| conduit_decl
	| ';'	/* Effectively empty */
	{ fprintf(stderr, "Found an empty statement\n"); }
	;

listen_stmt:
	LISTEN STRING ';'
	{
		/* The string gives the port to listen on */
	}
	| LISTEN '{' listen_block '}'
	{ fprintf(stderr, "Found listen+listen_block\n"); }
	;

listen_block:	listen_directives
	{ fprintf(stderr, "Found a listen_block\n"); }
	;

listen_directives:
	listen_directives listen_directive
	{ fprintf(stderr, "Found a listen_directives\n"); }
	|	/* Empty */
	{ fprintf(stderr, "Found an empty listen_directives\n"); }
	;

listen_directive:
	SERIAL ':' STRING ';'
	{
		fprintf(stderr, "Listen: serial [%s]\n", $2);
	}
	| SPEED ':' NUMBER ';'
	{
		fprintf(stderr, "Listen: speed %d\n", $2);
	}
	| PORT ':' NUMBER ';'
	{
		/* XXX - This isn't actually used. The idea is that
		 * this can be used to specify a TCP port on which to
		 * listen for a network connection.
		 */
		fprintf(stderr, "Listen: port %d]n", $2);
	}
	;

conduit_decl:	CONDUIT '{' conduit_block '}'

conduit_block:
	conduit_directives
	| conduit_directives conduit_args
	;

conduit_directives:
	conduit_directives conduit_directive
	|	/* Empty */
	;

conduit_directive:
	/* XXX - Empty */
	;

conduit_args:		/* Extra arguments passed to a conduit */
	ARGS ':' arglist
	;

arglist:	arglist arg
	| /* Empty */
	;

arg:	/* Empty */
	;

%%
