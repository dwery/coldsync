%{ /* -*- C -*- */
/* parser.y
 * Config file parser.
 *
 *	Copyright (C) 1999, 2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: parser.y,v 2.14 2000-04-10 09:35:53 arensb Exp $
 */
/* XXX - Variable assignments, manipulation, and lookup. */
/* XXX - Error-checking */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>		/* For malloc(), free() */
#include <string.h>		/* For strncpy() et al. */
#include <ctype.h>		/* For toupper() et al. */

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
static pda_block *cur_pda;		/* Currently-open PDA block. The
					 * various pda_directive rules will
					 * fill in the fields.
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
%token <crea_type>	CREA_TYPE
%token CONDUIT
%token DEFAULT
%token DEVICE
%token DIRECTORY
%token FINAL
%token LISTEN
%token NAME
%token PATH
%token PDA
%token SPEED
%token SNUM
%token TYPE

%token SERIAL
%token USB

%type <commtype> comm_type
%type <crea_type> creator_type

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
	comm_type commtype;
	crea_type_pair crea_type;
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
	| pda_stmt
	{ PARSE_TRACE(3)
		  fprintf(stderr, "Found a pda_stmt\n");
	}
	| ';'	/* Effectively empty */
	{ PARSE_TRACE(4)
		  fprintf(stderr, "Found an empty statement\n");
	}
	;

listen_stmt:
	LISTEN comm_type  '{'
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
		cur_listen->listen_type = $2;
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
			cur_listen = NULL;	/* So it doesn't get freed
						 * twice.
						 */
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
			cur_listen = NULL;	/* So it doesn't get freed
						 * twice.
						 */
		}
	}
	;

comm_type:
	SERIAL
	{
		PARSE_TRACE(5)
			fprintf(stderr, "Found commtype: Serial\n");
		$$ = LISTEN_SERIAL;
	}
	| USB
	{
		PARSE_TRACE(5)
			fprintf(stderr, "Found commtype: USB\n");
		$$ = LISTEN_USB;
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
			return -1;
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
			cur_conduit = NULL;	/* So it doesn't get freed
						 * twice.
						 */
		} else {
			/* Append conduit to the appropriate list */
			conduit_block *last;

			/* Go to the end of the list */
			last = *list;
			while (last->next != NULL)
				last = last->next;

			cur_conduit->next = NULL;
			last->next = cur_conduit;
			cur_conduit = NULL;	/* So it doesn't get freed
						 * twice.
						 */
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
	TYPE creator_type ';'
	/* XXX - This ought to take an optional argument saying that this
	 * conduit applies to just resource or record databases.
	 */
	{
		PARSE_TRACE(4)
		{
			fprintf(stderr, "Conduit creator: 0x%08ld (%c%c%c%c)\n",
				$2.creator,
				(char) (($2.creator >> 24) & 0xff),
				(char) (($2.creator >> 16) & 0xff),
				(char) (($2.creator >>  8) & 0xff),
				(char) ($2.creator & 0xff));
			fprintf(stderr, "Conduit type: 0x%08ld (%c%c%c%c)\n",
				$2.type,
				(char) (($2.type >> 24) & 0xff),
				(char) (($2.type >> 16) & 0xff),
				(char) (($2.type >>  8) & 0xff),
				(char) ($2.type & 0xff));
		}
		cur_conduit->dbcreator = $2.creator;
		cur_conduit->dbtype = $2.type;
	}
	| NAME STRING ';'	/* XXX - Is this used? */
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
	| DEFAULT ';'
	{
		PARSE_TRACE(4)
			fprintf(stderr, "This is a default conduit\n");

		/* Mark this conduit as being a fall-back default */
		cur_conduit->flags |= CONDFL_DEFAULT;	/* XXX - Test this */
	}
	| FINAL ';'
	{
		PARSE_TRACE(4)
			fprintf(stderr, "This is a final conduit\n");

		/* Mark this conduit as being final: if it matches, don't
		 * even look through the rest of the queue.
		 */
		cur_conduit->flags |= CONDFL_FINAL;	/* XXX - Test this */
	}
	;

creator_type:	STRING '/' STRING
	{
		/* Creator */
		if (strcmp($1, "*") == 0)
		{
			/* Wildcard creator */
			$$.creator = 0L;
		} else {
			/* Stated creator */
			if (strlen($1) != 4)
			{
				fprintf(stderr,
					_("%s: Bogus creator \"%s\", line "
					  "%d\n"),
					"yyparse",
					$1, lineno);
				free($1); $1 = NULL;
				free($3); $3 = NULL;
				YYERROR;
			}
			$$.creator =
				(($1[0]) << 24) |
				(($1[1]) << 16) |
				(($1[2]) << 8) |
				($1[3]);
		}

		/* Type */
		if (strcmp($3, "*") == 0)
		{
			/* Wildcard creator */
			$$.type = 0L;
		} else {
			/* Stated type */
			if (strlen($3) != 4)
			{
				fprintf(stderr,
					_("%s: Bogus type \"%s\", line "
					  "%d\n"),
					"yyparse",
					$3, lineno);
				free($1); $1 = NULL;
				free($3); $3 = NULL;
				YYERROR;
			}
			$$.creator =
				(($3[0]) << 24) |
				(($3[1]) << 16) |
				(($3[2]) << 8) |
				($3[3]);
		}
		free($1); $1 = NULL;
		free($3); $3 = NULL;
	}
	| CREA_TYPE
	{
		$$.creator = $1.creator;
		$$.type = $1.type;
	}
	;

pda_stmt:	PDA '{'
	{
		/* Create a new PDA block. Subsequent rules that parse
		 * substatements inside a 'pda' block will fill in fields
		 * in this struct.
		 */
		if ((cur_pda = new_pda_block()) == NULL)
		{
			fprintf(stderr,
				_("%s: Can't allocate PDA block\n"),
				"yyparse");
			return -1;
		}
	}
	pda_block
	'}'
	{
		PARSE_TRACE(3)
		{
			fprintf(stderr, "Found pda+pda_block:\n");
			fprintf(stderr, "\tS/N: [%s]\n",
				(cur_pda->snum == NULL ?
				 "(null)" :
				 cur_pda->snum));
			fprintf(stderr, "\tDirectory: [%s]\n",
				(cur_pda->directory == NULL ?
				 "(null)" :
				 cur_pda->directory));
			if ((cur_pda->flags & PDAFL_DEFAULT) != 0)
				fprintf(stderr, "\tDEFAULT\n");
		}

		if (file_config->pda == NULL)
		{
			/* This is the first pda block */
			PARSE_TRACE(3)
				fprintf(stderr,
					"Adding the first PDA block\n");

			file_config->pda = cur_pda;
			cur_pda = NULL;		/* So it doesn't get freed
						 * twice */
		} else {
			/* This is not the first pda block. Append it
			 * to the list.
			 */
			struct pda_block *last;

			PARSE_TRACE(3)
				fprintf(stderr,
					"Appending a PDA block to the list\n");

			/* Move forward to the last pda block on the
			 * list
			 */
			for (last = file_config->pda;
			     last->next != NULL;
			     last = last->next)
				;
			last->next = cur_pda;
			cur_pda = NULL;		/* So it doesn't get freed
						 * twice.
						 */
		}
	}
	;

pda_block:
	pda_directives
	;

pda_directives:
	pda_directives pda_directive
	|	/* Empty */
	;

pda_directive:
	SNUM STRING ';'
	{
		/* Serial number from ROM */
		char *csum_ptr;		/* Pointer to checksum character */
		unsigned char checksum;	/* Calculated checksum */

		PARSE_TRACE(4)
			fprintf(stderr, "Serial number \"%s\"\n", $2);

		cur_pda->snum = $2;
		$2 = NULL;

		/* Verify the checksum */
		csum_ptr = strrchr(cur_pda->snum, '-');

		if (strcmp(cur_pda->snum, "") == 0)
		{
			/* Serial number given as the empty string. This is
			 * fine. It specifies a PDA with no serial number
			 * (e.g., a PalmPilot).
			 */

		} else if (csum_ptr == NULL)
		{
			/* No checksum. Calculate it, and tell the user
			 * what it should be.
			 */
			checksum = snum_checksum(cur_pda->snum,
						 strlen(cur_pda->snum));
			fprintf(stderr, _("Warning: serial number \"%s\" "
					  "has no checksum. You may want\n"
					  "to rewrite it as \"%s-%c\"\n"),
				cur_pda->snum, cur_pda->snum, checksum);
		} else {
			/* Checksum specified in the config file. Make sure
			 * that it's correct. Warn the user if it isn't.
			 */

			*csum_ptr = '\0';	/* Truncate the string at
						 * the checksum. */
			csum_ptr++;		/* Point to the character
						 * after the dash, or the
						 * terminating NUL if the
						 * string is malformed.
						 */
			checksum = snum_checksum(cur_pda->snum,
						 strlen(cur_pda->snum));
			if (toupper(checksum) != toupper(*csum_ptr))
			{
				fprintf(stderr,
					_("Warning: incorrect checksum for "
					  "serial number \"%s-%c\".\n"
					  "Should be \"%s-%c\"\n"),
					cur_pda->snum, *csum_ptr,
					cur_pda->snum, checksum);
			}
		}
	}
	| DIRECTORY STRING ';'
	{
		PARSE_TRACE(4)
			fprintf(stderr, "Directory \"%s\"\n", $2);

		cur_pda->directory = $2;
		$2 = NULL;
	}
	| DEFAULT ';'
	{
		PARSE_TRACE(4)
			fprintf(stderr, "This is a default PDA\n");

		/* Mark this PDA as being the fallback default */
		cur_pda->flags |= PDAFL_DEFAULT;	/* XXX - Test this */
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
	if (cur_pda != NULL)
		free_pda_block(cur_pda);
	if (cur_conduit != NULL)
		free_conduit_block(cur_conduit);

	return -retval;
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
