%{ /* -*- C -*- */
/* parser.y
 * Config file parser.
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: parser.y,v 2.7 1999-11-27 05:54:52 arensb Exp $
 */
/* XXX - Variable assignments, manipulation, and lookup. */
/* XXX - Error-checking */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>		/* For malloc(), free() */
#include <string.h>		/* For strncpy() et al. */

#if HAVE_LIBINTL
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL */

#include "parser.h"

int parse_trace = 0;		/* Debugging level for config file parser */
#define PARSE_TRACE(n)	if (parse_trace >= (n))

extern int yylex(void);
extern int yyparse(void);

extern FILE *yyin;

int yyerror(const char *msg);

static listen_block *cur_listen;	/* Currently-open listen block. The
					 * various listen_directive rules
					 * will fill in the fields.
					 */
static conduit_block *cur_conduit;	/* Currently-open conduit block.
					 * The various conduit_directive
					 * rules will fill in the fields.
					 */
static struct config *file_config;	/* As the parser runs, it will fill
					 * in values in this struct.
					 */
%}

%token <integer>	NUMBER
%token <string>		STRING
%token CONDUIT
%token DEVICE
%token LISTEN
%token NAME
%token PATH
%token SERIAL
%token SPEED
%token TYPE

/* Conduit flavors */
%token SYNC
%token FETCH
%token DUMP
%token INSTALL
%token UNINSTALL

%type <flavor> conduit_flavor

%union {
	int integer;
	char *string;
	conduit_flavor flavor;
}

%%
file:	statements
	{ PARSE_TRACE(1)
		  fprintf(stderr, "Found a file\n");
	}
	;

statements:	statements statement
	{ PARSE_TRACE(3)
		  fprintf(stderr, "Found a statement\n");
	}
	|	/* Empty */
	{ PARSE_TRACE(4)
		  fprintf(stderr, "Found an empty statement\n");
	}
	;

statement:
	listen_stmt
	{ PARSE_TRACE(3)
		  fprintf(stderr, "Found a listen_stmt (statement)\n");
	}
	| conduit_stmt
	{ PARSE_TRACE(3)
		  fprintf(stderr, "Found a conduit_stmt\n");
	}
	| ';'	/* Effectively empty */
	{ PARSE_TRACE(4)
		  fprintf(stderr, "Found an empty statement\n");
	}
	;

listen_stmt:
	LISTEN SERIAL '{'
	{
		/* Create a new listen block. Subsequent rules that parse
		 * substatements inside a 'listen' block will fill in
		 * fields in this struct.
		 */
		if ((cur_listen = new_listen_block()) == NULL)
		{
			fprintf(stderr,
				_("%s: Can't allocate listen block\n"),
				"yyparse");
			return -1;
		}
		cur_listen->listen_type = LISTEN_SERIAL;
	}
	listen_block '}'
	{
		PARSE_TRACE(3)
		{
			fprintf(stderr, "Found listen+listen_block:\n");
			fprintf(stderr, "\tDevice: [%s]\n",
				cur_listen->device);
			fprintf(stderr, "\tSpeed: [%d]\n", cur_listen->speed);
		}

		if (file_config->listen == NULL)
		{
			/* This is the first listen block */
			file_config->listen = cur_listen;
			cur_listen = NULL;
		} else {
			/* This is not the first listen block. Append it to
			 * the list.
			 */
			struct listen_block *last;

			/* Move forward to the last listen block on the
			 * list
			 */
			for (last = file_config->listen;
			     last->next != NULL;
			     last = last->next)
				;
			last->next = cur_listen;
			cur_listen = NULL;
		}
	}
	;

listen_block:	listen_directives
	{ PARSE_TRACE(3)
		  fprintf(stderr, "Found a listen_block\n");
	}
	;

listen_directives:
	listen_directives listen_directive
	{ PARSE_TRACE(3)
		  fprintf(stderr, "Found a listen_directives\n");
	}
	|	/* Empty */
	;

listen_directive:
	DEVICE STRING ';'
	{
		PARSE_TRACE(4)
			fprintf(stderr, "Listen: device [%s]\n", $2);

		cur_listen->device = $2;
	}
	| SPEED NUMBER ';'
	{
		PARSE_TRACE(4)
			fprintf(stderr, "Listen: speed %d\n", $2);

		cur_listen->speed = $2;
	}
	;

conduit_stmt:	CONDUIT conduit_flavor '{'
	{
		cur_conduit = new_conduit_block();
		if (cur_conduit == NULL)
		{
			fprintf(stderr,
				_("%s: Can't allocate conduit_block!\n"),
				"yyparse");
			/* XXX - Try to recover gracefully */
			exit(1);
		}
		cur_conduit->flavor = $2;

		PARSE_TRACE(4)
			fprintf(stderr, "Found start of conduit [");

		switch (cur_conduit->flavor)
		{
		    case Sync:
			PARSE_TRACE(4)
				fprintf(stderr, "Sync");
			break;
		    case Fetch:
			PARSE_TRACE(4)
				fprintf(stderr, "Fetch");
			break;
		    case Dump:
			PARSE_TRACE(4)
				fprintf(stderr, "Dump");
			break;
		    case Install:
			PARSE_TRACE(4)
				fprintf(stderr, "Install");
			break;
		    case Uninstall:
			PARSE_TRACE(4)
				fprintf(stderr, "Uninstall");
			break;
		    default:
			fprintf(stderr, _("Unknown conduit flavor: %d"),
				cur_conduit->flavor);
			YYERROR;
		}
		PARSE_TRACE(4)
			fprintf(stderr, "]\n");
	}
	conduit_block '}'
	{
		/* Got a conduit block. Append it to the appropriate list. */
		conduit_block **list;

		switch (cur_conduit->flavor)
		{
		    case Sync:
			list = &(file_config->sync_q);
			break;
		    case Fetch:
			list = &(file_config->fetch_q);
			break;
		    case Dump:
			list = &(file_config->dump_q);
			break;
		    case Install:
			list = &(file_config->install_q);
			break;
		    case Uninstall:
			list = &(file_config->uninstall_q);
			break;
		    default:
			fprintf(stderr, _("%s: line %d: Unknown conduit "
					  "flavor %d\n"),
				"yyparse",
				lineno, cur_conduit->flavor);
			YYERROR;
		}

		if (*list == NULL)
		{
			/* First conduit on this list */
			*list = cur_conduit;
		} else {
			/* Append conduit to the appropriate list */
			conduit_block *last;

			/* Go to the end of the list */
			last = *list;
			while (last->next != NULL)
				last = last->next;

			cur_conduit->next = NULL;
			last->next = cur_conduit;
			cur_conduit = NULL;
		}
	}
	;

conduit_flavor:
	SYNC
	{
		PARSE_TRACE(5)
			fprintf(stderr, "Found a conduit_flavor: Sync\n");
		$$ = Sync;
	}
	| FETCH
	{
		PARSE_TRACE(5)
			fprintf(stderr, "Found a conduit_flavor: Fetch\n");
		$$ = Fetch;
	}
	| DUMP
	{
		PARSE_TRACE(5)
			fprintf(stderr, "Found a conduit_flavor: Dump\n");
		$$ = Dump;
	}
	| INSTALL
	{
		PARSE_TRACE(5)
			fprintf(stderr, "Found a conduit_flavor: Install\n");
		$$ = Install;
	}
	| UNINSTALL
	{
		PARSE_TRACE(5)
			fprintf(stderr, "Found a conduit_flavor: Uninstall\n");
		$$ = Uninstall;
	}
	;

conduit_block:
	conduit_directives
	;

conduit_directives:
	conduit_directives conduit_directive
	|	/* Empty */
	;

conduit_directive:
	TYPE STRING '/' STRING ';'
	{
		if (strcmp($2, "*") == 0)
		{
			/* Wildcard creator */
			cur_conduit->dbcreator = 0L;
		} else {
			/* Stated type */
			if (strlen($2) != 4)
			{
				fprintf(stderr,
					_("%s: Bogus creator \"%s\", line "
					  "%d\n"),
					"yyparse",
					$2, lineno);
				free($2); $2 = NULL;
				free($4); $4 = NULL;
				YYERROR;
			}
			cur_conduit->dbcreator =
				(($2[0]) << 24) |
				(($2[1]) << 16) |
				(($2[2]) << 8) |
				($2[3]);
		}

		if (strcmp($4, "*") == 0)
		{
			/* Wildcard type */
			cur_conduit->dbtype = 0L;
		} else {
			/* Stated type */
			if (strlen($4) != 4)
			{
				fprintf(stderr,
					_("%s: Bogus type \"%s\", line %d\n"),
					"yyparse",
					$4, lineno);
				cur_conduit->dbtype = 0L;
				free($2); $2 = NULL;
				free($4); $4 = NULL;
				YYERROR;
			}
			cur_conduit->dbtype =
				(($4[0]) << 24) |
				(($4[1]) << 16) |
				(($4[2]) << 8) |
				($4[3]);
		}
		PARSE_TRACE(4)
			fprintf(stderr, "Conduit type: [%s]/[%s]\n", $2, $4);
		free($2); $2 = NULL;
		free($4); $4 = NULL;
	}
	| NAME STRING ';'
	| PATH STRING ';'
	{
		/* Path to the conduit program. If this is a relative
		 * pathname, look for it in the path.
		 * XXX - There should be a ConduitPath directive to specify
		 * where to look for conduits.
		 */
		cur_conduit->path = $2;
		$2 = NULL;

		PARSE_TRACE(4)
			fprintf(stderr, "Conduit path: [%s]\n",
				cur_conduit->path);
	}
	;

%%

/* yyerror
 * Print out an error message about the error that just occurred.
 */
int
yyerror(const char *msg)
{
	fprintf(stderr, _("Yacc error: \"%s\" at line %d\n"), msg, lineno);
	return 1;
}

/* parse_config
 * Parse the given config file.
 */
int parse_config(const char *fname,
		 struct config *conf)
{
	FILE *infile;
	int retval;

	if ((infile = fopen(fname, "r")) == NULL)
	{
		fprintf(stderr, _("%s: Can't open \"%s\"\n"),
			"parse_config", fname);
		perror("fopen");
		return -1;
	}

	yyin = infile;
	lineno = 1;
	file_config = conf;
	retval = yyparse();
	fclose(infile);

	if (cur_listen != NULL)
		free_listen_block(cur_listen);
	if (cur_conduit != NULL)
		free_conduit_block(cur_conduit);

	return -retval;
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
