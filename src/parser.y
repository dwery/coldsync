%{ /* -*- C -*- */
/* parser.y
 * Config file parser.
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id$
 */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>		/* For malloc(), free() */
#include <string.h>		/* For strncpy() et al. */
#include <ctype.h>		/* For toupper() et al. */

#if HAVE_LIBINTL_H
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL_H */

#include "parser.h"
#include "symboltable.h"
#include "trace.h"

#define YYDEBUG 1

int parse_trace = 0;		/* Debugging level for config file parser */

#define ANOTHER_ERROR \
	{ if (++num_errors > 10) \
	  { \
		  Error(_("Too many errors. Aborting.")); \
		  return -1; \
	  } \
	}

extern int yylex(void);
extern int yyparse(void);

extern FILE *yyin;

extern char *yytext;			/* Reaching in to lex's namespace */

int yyerror(const char *msg);

static const char *conf_fname;		/* Name of config file. Used for
					 * error-reporting.
					 */
static int num_errors = 0;		/* # of errors seen during parsing */
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
static struct sync_config *file_config;	/* As the parser runs, it will fill
					 * in values in this struct.
					 */
%}

%token <crea_type>	CREA_TYPE
%token <boolean>	FALSE
%token <integer>	NUMBER
%token <integer>	USERID
%token <string>		STRING
%token <boolean>	TRUE
%token <string>		USERNAME
%token <string>		WORD
%token ARGUMENTS
%token CONDUIT
%token DEFAULT
%token DEVICE
%token DIRECTORY
%token FINAL
%token FORCE_INSTALL
%token FORWARD
%token HOSTID
%token INSTALL_FIRST
%token AUTOINIT
%token AUTORESCUE
%token FILTER_DBS
%token LISTEN
%token OPTIONS
%token PATH
%token CWD
%token ENABLED
%token PDA
%token PREFERENCE
%token SAVED
%token SPEED
%token SNUM
%token TRANSIENT
%token NOCHANGESPEED
%token NOPROMPT
%token TYPE
%token UNSAVED
%token USE_CARD_SERIAL

%token SERIAL
%token USB
%token LIBUSB
%token NET

%token PROTOCOL
%token FULL
%token SIMPLE

%type <boolean> boolean
%type <commtype> comm_type
%type <crea_type> creator_type
%type <string> opt_pda_name
%type <string> opt_listen_name
%type <integer> opt_pref_flag
%type <string> opt_string
%type <proto_type> protocol_stack

/* Conduit flavors */
%token SYNC
%token FETCH
%token DUMP
%token INSTALL
%token INIT

%union {
	long integer;
	char *string;
	pconn_listen_t commtype;
	pconn_proto_t proto_type;
	crea_type_pair crea_type;
	Bool3 boolean;
}

%%
file:	statements
	{ PARSE_TRACE(1)
		  fprintf(stderr, "Found a configuration file\n");
	}
	;

statements:	statements statement
	{ PARSE_TRACE(6)
		  fprintf(stderr, "Found a statement\n");
	}
	|	/* Empty */
	{ PARSE_TRACE(7)
		  fprintf(stderr, "Found an empty statement\n");
	}
	;

statement:
	listen_stmt
	{ PARSE_TRACE(6)
		  fprintf(stderr, "Found a listen_stmt (statement)\n");
	}
	| conduit_stmt
	{ PARSE_TRACE(6)
		  fprintf(stderr, "Found a conduit_stmt\n");
	}
	| options_stmt
	{ PARSE_TRACE(6)
		  fprintf(stderr, "Found an options_stmt\n");
	}
	| pda_stmt
	{ PARSE_TRACE(6)
		  fprintf(stderr, "Found a pda_stmt\n");
	}
	| ';'	/* Effectively empty */
	{ PARSE_TRACE(7)
		  fprintf(stderr, "Found an empty statement\n");
	}
	;

listen_stmt:
	LISTEN comm_type opt_listen_name open_brace
	{
		/* Create a new listen block. Subsequent rules that parse
		 * substatements inside a 'listen' block will fill in
		 * fields in this struct.
		 */
		if ((cur_listen = new_listen_block()) == NULL)
		{
			Error(_("%s: Can't allocate listen block."),
			      "yyparse");
			return -1;
		}
		cur_listen->listen_type = $2;
		cur_listen->name = $3;
	}
	listen_block '}'
	{
		PARSE_TRACE(3)
		{
			fprintf(stderr, "Found listen+listen_block:\n");
			fprintf(stderr, "\tName: [%s]\n",
				(cur_listen->name == NULL ? "(null)" :
				 cur_listen->name));
			fprintf(stderr, "\tDevice: [%s]\n",
				(cur_listen->device == NULL ? "(null)" :
				 cur_listen->device));
			fprintf(stderr, "\tSpeed: [%ld]\n", cur_listen->speed);
			fprintf(stderr, "\tProtocol: %d\n",
				(int) cur_listen->protocol);
                        fprintf(stderr, "\tFlags:");  
                        if ((cur_listen->flags & LISTENFL_PROMPT) != 0)
                                fprintf(stderr, " PROMPT");
			if ((cur_listen->flags & LISTENFL_NOCHANGESPEED) != 0)
                                fprintf(stderr, " NOCHANGESPEED");
                      	if ((cur_listen->flags & LISTENFL_TRANSIENT) != 0)
                                fprintf(stderr, " TRANSIENT");
		        fprintf(stderr, "\n");
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
			fprintf(stderr, "Listen: commtype serial\n");
		$$ = LISTEN_SERIAL;
	}
	| USB
	{
		PARSE_TRACE(5)
			fprintf(stderr, "Listen: commtype USB\n");
		$$ = LISTEN_USB;
	}
	| LIBUSB
	{
		PARSE_TRACE(5)
			fprintf(stderr, "Listen: commtype LIBUSB\n");
		$$ = LISTEN_LIBUSB;
	}
	| NET
	{
		PARSE_TRACE(5)
			fprintf(stderr, "Listen: commtype net\n");
		$$ = LISTEN_NET;
	}
	| error
	{
		ANOTHER_ERROR;
		if (yytext[0] == '{')
		{
			Error(_("\tMissing listen block type."));
		} else {
			Error(_("\tUnrecognized listen type \"%s\"."),
			      yytext);
			yyclearin; 
		}
		$$ = LISTEN_SERIAL;
	}
	;

opt_listen_name: STRING
	|	/* Empty */
	{
		$$ = NULL;
	}
	;


listen_block:	listen_directives
	{ PARSE_TRACE(3)
		  fprintf(stderr, "Found a listen_block\n");
	}
	;

listen_directives:
	listen_directives listen_directive
	{ PARSE_TRACE(6)
		  fprintf(stderr, "Found a listen_directive\n");
	}
	|	/* Empty */
	;

listen_directive:
	DEVICE colon
	{
		lex_expect(LEX_BSTRING);
	}
	STRING semicolon
	{
		PARSE_TRACE(4)
			fprintf(stderr, "\tListen: device [%s]\n", $4);

		lex_expect(LEX_NONE);

		if (cur_listen->device != NULL)
		{
			Error(_("%s: %d: Device already defined."),
			      conf_fname, lineno);
			free(cur_listen->device);
		}
		cur_listen->device = $4;
		$4 = NULL;
	}
	| SPEED colon NUMBER semicolon
	{
		PARSE_TRACE(4)
			fprintf(stderr, "\tListen: speed %ld\n", $3);

		if (cur_listen->speed != 0)
		{
			Error(_("%s: %d: Speed already defined."),
			      conf_fname, lineno);
		}

		cur_listen->speed = $3;
	}
	| PROTOCOL colon protocol_stack semicolon
	{
		PARSE_TRACE(4)
			fprintf(stderr, "\tListen: protocol %d\n", (int) $3);

		cur_listen->protocol = $3;
			/* XXX - Would be nice to be able to tell whether
			 * the protocol has already been specified.
			 */
	}
	| TRANSIENT semicolon
	{
		PARSE_TRACE(4)
			fprintf(stderr, "This is a transient device.\n");

		/* Mark this device as being transient */
		cur_listen->flags |= LISTENFL_TRANSIENT;
	}
	| NOCHANGESPEED semicolon
	{
		PARSE_TRACE(4)
			fprintf(stderr, "This is a modem device.\n");

		/* Mark this device as being a modem */
		cur_listen->flags |= LISTENFL_NOCHANGESPEED;
	}
	| NOPROMPT semicolon
	{
		PARSE_TRACE(4)
			fprintf(stderr, "Don't prompt for the HotSync button.\n");

		cur_listen->flags &= ~LISTENFL_PROMPT;
	}
	| error
	{
		Error(_("\tError near \"%s\"."),
		      yytext);
		ANOTHER_ERROR;
		yyclearin;
	}
	';'
	;

conduit_stmt:	CONDUIT
	{
		cur_conduit = new_conduit_block();
		if (cur_conduit == NULL)
		{
			Error(_("%s: Can't allocate conduit_block!"),
			      "yyparse");
			return -1;
		}

		PARSE_TRACE(2)
		{
			fprintf(stderr, "Found conduit_block\n");
		}

	} flavor_list '{'
	conduit_block opt_header_list '}'
	{
		lex_expect(LEX_NONE);		/* No special lexer context */

		PARSE_TRACE(3)
		{
			fprintf(stderr, "Found conduit+conduit_block:\n");
			fprintf(stderr, "\tEnabled: [%s]\n",
				cur_conduit->enabled ? "Yes" : "No");
			fprintf(stderr, "\tPath: [%s]\n",
				(cur_conduit->path == NULL ?
				 "(null)" :
				 cur_conduit->path));
			fprintf(stderr, "\tCwd: [%s]\n",
				(cur_conduit->cwd == NULL ?
				 "(undefined)" :
				 cur_conduit->cwd));
			fprintf(stderr, "\tFlags:");

			if (cur_conduit->flags & CONDFL_FINAL)
				fprintf(stderr, " final");

			if (cur_conduit->flags & CONDFL_DEFAULT)
				fprintf(stderr, " default");

			if (cur_conduit->flags == 0L)
				fprintf(stderr, " none");

			fprintf(stderr, "\n");
		}


		/* Sanity check */
		if (cur_conduit->num_ctypes == 0)
		{
			Error(_("%s: No `type:' line seen\n"
				"\tin definition of \"%s\"."),
			      conf_fname,
			      cur_conduit->path);
		}

		if (file_config->conduits == NULL)
		{
			/* First conduit on this list */
			file_config->conduits = cur_conduit;
			cur_conduit = NULL;	/* So it doesn't get freed
						 * twice.
						 */
		} else {
			/* Append conduit to the appropriate list */
			conduit_block *last;

			/* Go to the end of the list */
			last = file_config->conduits;
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

flavor_list:	flavor
	| flavor_list ',' flavor
	;

flavor:
	SYNC
	{
		PARSE_TRACE(5)
			fprintf(stderr, "Conduit: flavor Sync\n");
		cur_conduit->flavors |= FLAVORFL_SYNC;
	}
	| FETCH
	{
		PARSE_TRACE(5)
			fprintf(stderr, "Conduit: flavor Fetch\n");
		cur_conduit->flavors |= FLAVORFL_FETCH;
	}
	| DUMP
	{
		PARSE_TRACE(5)
			fprintf(stderr, "Conduit: flavor Dump\n");
		cur_conduit->flavors |= FLAVORFL_DUMP;
	}
	| INSTALL
	{
		PARSE_TRACE(5)
			fprintf(stderr, "Conduit: flavor Install\n");
		cur_conduit->flavors |= FLAVORFL_INSTALL;
	}
	| INIT
	{
		PARSE_TRACE(5)
			fprintf(stderr, "Conduit: flavor Init\n");
		cur_conduit->flavors |= FLAVORFL_INIT;
	}
	| error
	{
		ANOTHER_ERROR;
		Error(_("\tUnrecognized conduit flavor \"%s\"."),
		      yytext);
		yyclearin;
	}
	;

protocol_stack:
	DEFAULT
	{
		PARSE_TRACE(5)
			fprintf(stderr, "Listen: protocol stack: default\n");
		$$ = PCONN_STACK_DEFAULT;
	}
	| FULL
	{
		PARSE_TRACE(5)
			fprintf(stderr, "Listen: protocol stack: full\n");
		$$ = PCONN_STACK_FULL;
	}
	| SIMPLE
	{
		PARSE_TRACE(5)
			fprintf(stderr, "Listen: protocol stack: simple\n");
		Warn(_("The \"simple\" protocol is now deprecated, use \"net\" instead."));
		$$ = PCONN_STACK_NET;
	}
	| NET
	{
		PARSE_TRACE(5)
			fprintf(stderr, "Listen: protocol stack: net\n");
		$$ = PCONN_STACK_NET;
	}
	| error
	{
		ANOTHER_ERROR;
		Error(_("\tUnrecognized protocol: \"%s\"."),
		      yytext);
		yyclearin;
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
	TYPE
	{
		lex_expect(LEX_CTPAIR);
	}
	colon creator_type semicolon
	/* XXX - This ought to take an optional argument saying that this
	 * conduit applies to just resource or record databases.
	 */
	{
		int err;

		PARSE_TRACE(4)
		{
			fprintf(stderr,
				"\tConduit: creator 0x%08ld (%c%c%c%c)\n",
				$4.creator,
				(char) (($4.creator >> 24) & 0xff),
				(char) (($4.creator >> 16) & 0xff),
				(char) (($4.creator >>  8) & 0xff),
				(char) ($4.creator & 0xff));
			fprintf(stderr,
				"\tConduit: type 0x%08ld (%c%c%c%c)\n",
				$4.type,
				(char) (($4.type >> 24) & 0xff),
				(char) (($4.type >> 16) & 0xff),
				(char) (($4.type >>  8) & 0xff),
				(char) ($4.type & 0xff));

			fprintf(stderr, "\tConduit: flags 0x%02x\n",
				$4.flags);
		}

		lex_expect(LEX_NONE);

		if ((err = append_crea_type(cur_conduit, $4.creator,
						$4.type, $4.flags))
		    < 0)
		{
			Error(_("%s: %d: Can't add creator-type pair to "
				"list. This is very bad."),
			      conf_fname, lineno);
			return -1;
		}
	}
	| PATH colon
	{
		lex_expect(LEX_BSTRING);
	}
	STRING semicolon
	{
		lex_expect(LEX_NONE);

		if (cur_conduit->path != NULL)
		{
			Warn(_("%s: %d: Path already defined."),
			     conf_fname, lineno);
			free(cur_conduit->path);
		}

		/* Path to the conduit program. If this is a relative
		 * pathname, look for it in the path.
		 */
		cur_conduit->path = $4;
		$4 = NULL;

		PARSE_TRACE(4)
			fprintf(stderr, "\tConduit: path [%s]\n",
				cur_conduit->path);
	}
	| CWD colon
	{
		lex_expect(LEX_BSTRING);
	}
	STRING semicolon
	{
		lex_expect(LEX_NONE);

		if (cur_conduit->cwd != NULL)
		{
			Warn(_("%s: %d: Cwd re-defined."),
			     conf_fname, lineno);
			free(cur_conduit->cwd);
		}

		cur_conduit->cwd = $4;
		$4 = NULL;

		PARSE_TRACE(4)
			fprintf(stderr, "\tConduit: cwd [%s]\n",
				cur_conduit->cwd);
	}
        | ENABLED colon boolean ';'
        {
	 	PARSE_TRACE(3)
			fprintf(stderr, "\tConduit: enabled.\n");
                cur_conduit->enabled = $3;
        }
        | ENABLED ';'
        {
         	PARSE_TRACE(3)
	                fprintf(stderr, "\tConduit: enabled.\n");
        	cur_conduit->enabled = True;
        }
	| PREFERENCE colon
	{
		lex_expect(LEX_ID4);
	}
	opt_pref_flag STRING
	{
		lex_expect(LEX_NONE);
	}
	'/' NUMBER semicolon
	{
		udword creator;
		int err;

		PARSE_TRACE(4)
			fprintf(stderr, "Preference: [%s]/%ld\n",
				$5, $8);

		creator =
			(($5[0]) << 24) |
			(($5[1]) << 16) |
			(($5[2]) << 8) |
			($5[3]);
		if ((err = append_pref_desc(cur_conduit, creator, $8, $4)) < 0)
		{
			Error(_("%s: %d: Can't add preference to list. "
				"This is very bad."),
			      conf_fname, lineno);
			free($5); $5 = NULL;
			return -1;
		}

		free($5);
		$5 = NULL;
	}
	| DEFAULT semicolon
	{
		lex_expect(LEX_NONE);

		PARSE_TRACE(4)
			fprintf(stderr, "This is a default conduit\n");

		/* Mark this conduit as being a fall-back default */
		cur_conduit->flags |= CONDFL_DEFAULT;
	}
	| FINAL semicolon
	{
		PARSE_TRACE(4)
			fprintf(stderr, "This is a final conduit\n");

		/* Mark this conduit as being final: if it matches, don't
		 * even look through the rest of the queue.
		 */
		cur_conduit->flags |= CONDFL_FINAL;
	}
	| error
	{
		Error(_("\tError near \"%s\"."),
		      yytext);
		ANOTHER_ERROR;
		yyclearin;
	}
	';'
	;

/* opt_pref_flags: optional `preference' statement modifier.
 */
opt_pref_flag:	SAVED
	{ $$ = PREFDFL_SAVED; }
	| UNSAVED
	{ $$ = PREFDFL_UNSAVED; }
	|	/* Empty */
	{ $$ = 0; }
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
				Error(_("%s: %d: Bogus creator \"%s\"."),
				      conf_fname, lineno,
				      $1);
				free($1); $1 = NULL;
				free($3); $3 = NULL;
				ANOTHER_ERROR;
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
				Error(_("%s: %d: Bogus type \"%s\"."),
				      conf_fname, lineno,
				      $3);
				free($1); $1 = NULL;
				free($3); $3 = NULL;
				ANOTHER_ERROR;
				YYERROR;
			}
			$$.type =
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

/* Optional list of user-supplied headers */
opt_header_list:	ARGUMENTS ':'
	{
		lex_expect(LEX_HEADER);
	}
	header_list
	{
		/* This resets the leftover state from the last header
		 * line.
		 */
		lex_expect(LEX_NONE);
	}
	|	/* Empty */
	{
		lex_expect(LEX_NONE);
	}
	;

/* header_list: a possibly-empty list of user-supplied headers */
/* NOTE: at the beginning of the first (nonempty list) clause, we are
 * expecting to see a header name (LEX_HEADER). This is set by the other
 * (error and empty list) clauses. I'm sorry for violating locality this
 * way, but it was necessary.
 */
header_list:	header_list
	STRING colon
	{
		lex_expect(LEX_BSTRING);
	}
	STRING semicolon
	{
		struct cond_header *new_hdr;

		PARSE_TRACE(3)
		{
			fprintf(stderr, "Found header list\n");
			fprintf(stderr, "New header: [%s]->[%s]\n",
				$2, $5);
		}

		/* Append the new header to the current conduit_block's
		 * header list.
		 */
		if ((new_hdr = (struct cond_header *)
		     malloc(sizeof(struct cond_header))) == NULL)
		{
			Error(_("%s: Can't allocate conduit header."),
			      "yyparse");
			return -1;
		}

		/* Initialize the new header */
		new_hdr->next = NULL;
		new_hdr->name = $2; $2 = NULL;
		new_hdr->value = $5; $5 = NULL;

		/* Append the new header to the list in the current conduit
		 * block.
		 */
		if (cur_conduit->headers == NULL)
		{
			/* This is the first header */
			cur_conduit->headers = new_hdr;
		} else {
			struct cond_header *last_hdr;
						/* Last header on the list */

			/* Find the last header on the list. This isn't the
			 * most efficient way to do things, but efficiency
			 * isn't a big concern here.
			 */
			for (last_hdr = cur_conduit->headers;
			     last_hdr->next != NULL;
			     last_hdr = last_hdr->next)
				;
			last_hdr->next = new_hdr;
		}

		lex_expect(LEX_HEADER);		/* Prepare for the next line */
	}
	|	/* Empty */
	{
		PARSE_TRACE(3)
			fprintf(stderr, "Found empty header list\n");
		lex_expect(LEX_HEADER);
	}
	| header_list ':' error
	{
		Error(_("\tMissing argument name near \": %s\"."),
		      yytext);
		ANOTHER_ERROR;
		yyclearin;
		lex_expect(LEX_HEADER);
	}
	';'
	;

options_stmt: OPTIONS open_brace
	{
		lex_expect(LEX_VAR);
	}
        options_list
	{
		lex_expect(LEX_NONE);
	}
	'}'
	;

options_list: options_list option
	| /* Empty */
	;

option:	FORCE_INSTALL colon boolean ';'
	{
		PARSE_TRACE(3)
			fprintf(stderr, "Option: force_install.\n");

		file_config->options.force_install = $3;
	}
	| FORCE_INSTALL ';'
	{
		PARSE_TRACE(3)
			fprintf(stderr, "Option: force_install.\n");
		file_config->options.force_install = True3;
	}
	| INSTALL_FIRST colon boolean ';'
	{
		PARSE_TRACE(3)
			fprintf(stderr, "Option: install_first.\n");
		file_config->options.install_first = $3;
	}
	| INSTALL_FIRST ';'
	{
		PARSE_TRACE(3)
			fprintf(stderr, "Option: install_first.\n");
		file_config->options.install_first = True3;
	}
	| AUTOINIT colon boolean ';'
	{
		PARSE_TRACE(3)
			fprintf(stderr, "Option: autoinit.\n");
		file_config->options.autoinit = $3;
	}
	| AUTOINIT ';'
	{
		PARSE_TRACE(3)
			fprintf(stderr, "Option: autoinit.\n");
		file_config->options.autoinit = True3;
	}
	| AUTORESCUE colon boolean ';'
	{
		PARSE_TRACE(3)
			fprintf(stderr, "Option: autorescue.\n");
		file_config->options.autoinit = $3;
	}
	| AUTORESCUE ';'
	{
		PARSE_TRACE(3)
			fprintf(stderr, "Option: autorescue.\n");
		file_config->options.autorescue = True3;
	}
	| FILTER_DBS colon boolean ';'
	{
		PARSE_TRACE(3)
			fprintf(stderr, "Option: filter_dbs.\n");
		file_config->options.filter_dbs = $3;
	}
	| FILTER_DBS ';'
	{
		PARSE_TRACE(3)
			fprintf(stderr, "Option: filter_dbs.\n");
		file_config->options.filter_dbs = True3;
	}
	| USE_CARD_SERIAL colon boolean ';'
	{
		PARSE_TRACE(3)
			fprintf(stderr, "Option: use_card_serial.\n");
		file_config->options.use_card_serial = $3;
	}
	| USE_CARD_SERIAL ';'
	{
		PARSE_TRACE(3)
			fprintf(stderr, "Option: use_card_serial.\n");
		file_config->options.use_card_serial = True3;
	}
	| HOSTID colon NUMBER semicolon
	{
		PARSE_TRACE(3)
			fprintf(stderr, "Option: hostid: [%lu]\n",
				$3);
		hostid = $3;
	}
	/* XXX - This is still broken: it accepts assignments of the form
	 *	options {
	 *		"var-iable": value;
	 *	}
	 * Which may or may not be wrong, but doesn't feel right.
	 */
        | STRING colon
    	{
		lex_expect(LEX_BSTRING);
	}
	STRING semicolon
	{
		PARSE_TRACE(3)
			fprintf(stderr, "Found symbol: %s ==> %s\n",
				$1, $4);
		lex_expect(LEX_NONE);
		if (put_symbol($1, $4) < 0)
		{
			Error(_("%s: Can't set option."),
			      "yyparse");
			return -1;
		}
		$1 = NULL;
		$4 = NULL;
		lex_expect(LEX_VAR);		/* Prepare for the next line */
	}
	| error ';'
	{
		Error(_("\tMissing option name near \"%s\"."),
		      yytext);
		ANOTHER_ERROR;
		yyclearin;
		lex_expect(LEX_VAR);
	}
	;

pda_stmt:	PDA
	{
		lex_expect(LEX_BSTRING);
	}
	opt_pda_name open_brace
	{
		lex_expect(LEX_NONE);

		/* Create a new PDA block. Subsequent rules that parse
		 * substatements inside a 'pda' block will fill in fields
		 * in this struct.
		 */
		if ((cur_pda = new_pda_block()) == NULL)
		{
			Error(_("%s: Can't allocate PDA block."),
			      "yyparse");
			return -1;
		}

		/* Initialize the name */
		cur_pda->name = $3;
		$3 = NULL;

		PARSE_TRACE(2)
		{
			fprintf(stderr, "Found pda_block, ");
			if (cur_pda->name == NULL)
				fprintf(stderr, "no name\n");
			else
				fprintf(stderr, "name [%s]\n", cur_pda->name);
		}
	}
	pda_block '}'
	{
		PARSE_TRACE(3)
		{
			fprintf(stderr, "Found pda+pda_block:\n");
			fprintf(stderr, "\tUsername: [%s]\n",
				(cur_pda->username == NULL ?
				 "(null)" :
				 cur_pda->username));
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
			PARSE_TRACE(5)
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

			PARSE_TRACE(5)
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

opt_pda_name:	STRING
	|	/* Empty */
	{
		$$ = NULL;
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
	SNUM colon
	{
		lex_expect(LEX_BSTRING);
	}
	STRING semicolon
	{
		/* Serial number from ROM */
		char *csum_ptr;		/* Pointer to checksum character */
		unsigned char checksum;	/* Calculated checksum */

		PARSE_TRACE(4)
			fprintf(stderr, "\tPDA: serial number \"%s\"\n", $4);

		lex_expect(LEX_NONE);

		if (cur_pda->snum != NULL)
		{
			Warn(_("%s: %d: Serial number already defined."),
			     conf_fname, lineno);
			free(cur_pda->snum);
		}

		cur_pda->snum = $4;
		$4 = NULL;

		/* Verify the checksum */
		csum_ptr = strrchr(cur_pda->snum, '-');

		if (strcmp(cur_pda->snum, "") == 0)
		{
			/* Serial number given as the empty string. This is
			 * fine. It specifies a PDA with no serial number
			 * (e.g., a PalmPilot).
			 */
		} else if (cur_pda->snum[0] == '*')
		{
			/* If the serial number begins with '*', assume
			 * that it's a special serial number (e.g.,
			 * "*Visor*") and that therefore it doesn't need a
			 * serial number.
			 */
		} else if (csum_ptr == NULL)
		{
			/* No checksum. Calculate it, and tell the user
			 * what it should be.
			 */
			checksum = snum_checksum(cur_pda->snum,
						 strlen(cur_pda->snum));
			Warn(_("%s: %d: Serial number \"%s\" has no "
			       "checksum.\n"
			       "You may want to rewrite it as \"%s-%c\"."),
			     conf_fname, lineno,
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
			if (toupper(checksum) != toupper((int) *csum_ptr))
			{
				Warn(_("%s: %d: Incorrect checksum\n"
				       "for serial number \"%s-%c\". "
				       "Should be \"%s-%c\"."),
				     conf_fname, lineno,
				     cur_pda->snum, *csum_ptr,
				     cur_pda->snum, checksum);
			}
			/* XXX - If the checksum is invalid, then
			 * presumably the serial number should be
			 * considered invalid (by fiat) as well. Since the
			 * serial number is invalid, this entire PDA block
			 * is suspect. In fact, it might be a good idea to
			 * abort at this point (after processing the file,
			 * that is).
			 */
		}
	}
	| DIRECTORY colon
	{
		lex_expect(LEX_BSTRING);
	}
	STRING semicolon
	{
		PARSE_TRACE(4)
			fprintf(stderr, "\tPDA: directory \"%s\"\n", $4);

		lex_expect(LEX_NONE);

		if (cur_pda->directory != NULL)
		{
			Warn(_("%s: %d: Directory already defined."),
			     conf_fname, lineno);
			free(cur_pda->directory);
		}

		cur_pda->directory = $4;
		$4 = NULL;
	}
	| USERNAME colon
	{
		lex_expect(LEX_BSTRING);
	}
	STRING semicolon
	{
		PARSE_TRACE(4)
			fprintf(stderr, "\tPDA: username \"%s\"\n", $4);

		lex_expect(LEX_NONE);

		if (cur_pda->username != NULL)
		{
			Warn(_("%s: %d: Username already defined."),
			     conf_fname, lineno);
			free(cur_pda->username);
		}

		cur_pda->username = $4;
		$4 = NULL;
	}
	| USERID colon NUMBER semicolon
	{
		PARSE_TRACE(4)
			fprintf(stderr, "\tPDA: userid \"%ld\"\n", $3);

		if (cur_pda->userid_given)
		{
			Warn(_("%s: %d: Userid already defined."),
			     conf_fname, lineno);
		}

		cur_pda->userid_given = True;
		cur_pda->userid = $3;
	}
	| FORWARD colon
	{
		lex_expect(LEX_BSTRING);
	}
	STRING
	{
		lex_expect(LEX_BSTRING);
	}
	opt_string semicolon
	{
		/* Found a "forward:" directive.
		 * forward: *;
		 *	Forward to wherever the Palm says.
		 * forward: hostname;
		 *	Forward to <hostname>.
		 * forward: hostname alias;
		 *	Forward to <hostname> and use <alias> in the
		 *	NetSync wakeup packet.
		 */
		PARSE_TRACE(4)
			fprintf(stderr, "\tPDA: forward \"%s\" \"%s\"\n",
				$4, ($6 == NULL ? "(null)" : $6));
		lex_expect(LEX_NONE);

		cur_pda->forward = True;

		/* Get the name of the host to forward to */
		if (strcmp($4, "*") == 0)
		{
			/* forward: *;
			 * means "forward to whatever the Palm says.
			 */
			cur_pda->forward_host = NULL;
		} else {
			cur_pda->forward_host = $4;
			$4 = NULL;
		}

		/* Get the name to use in the connection */
		if ($6 != NULL)
		{
			cur_pda->forward_name = $6;
			$6 = NULL;
		}
	}
	| DEFAULT semicolon
	{
		PARSE_TRACE(4)
			fprintf(stderr, "This is a default PDA\n");

		/* Mark this PDA as being the fallback default */
		cur_pda->flags |= PDAFL_DEFAULT;
	}
	| error
	{
		Error(_("\tError near \"%s\"."),
		      yytext);
		ANOTHER_ERROR;
		yyclearin;
	}
	';'
	;

opt_string:	STRING
	|	/* Empty */
	{
		$$ = NULL;
	}
	;

boolean:	TRUE
	{
		$$ = True3;
	}
	| FALSE
	{
		$$ = False3;
	}
	;

colon:	':'
	| error
	{
		ANOTHER_ERROR;
		Error(_("\tMissing ':'."));
	}
	;

open_brace:	'{'
	| error
	{
		ANOTHER_ERROR;
		Error(_("\tMissing '{'."));
	}
	;

semicolon:	';'
	| error
	{
		ANOTHER_ERROR;
		Error(_("\tMissing ';'."));
	}
	;

%%

/* yyerror
 * Print out an error message about the error that just occurred.
 */
/* XXX - I18n is broken: yacc/bison's error messages aren't translated. The
 * simple thing to do would be to get a list of all messages that appear in
 * yacc/bison's generated grammars, and stick them in an array:
 *	char *bison_msgs[] = {
 *		N_("parser stack overflow"),
 *		...
 *	};
 * Then stick this table in a file that won't be compiled, but which can be
 * fed to 'i18n/xgettext'.
 *
 * The general case is impossible to fix, since bison with #define
 * YYERROR_VERBOSE appearse to print messages with arbitrary contents.
 */
int
yyerror(const char *msg)
{
	Error("%s: %d: %s", conf_fname, lineno, _(msg));
	return 1;
}

/* parse_config_file
 * Parse the given config file.
 */
int parse_config_file(const char *fname,
		      struct sync_config *conf)
{
	FILE *infile;
	int retval;
/*  	yydebug = 1; */

	if ((infile = fopen(fname, "r")) == NULL)
	{
		Error(_("%s: Can't open \"%s\"."),
		      "parse_config_file", fname);
		Perror("fopen");
		return -1;
	}

	yyin = infile;
	conf_fname = fname;
	lineno = 1;
	file_config = conf;
	num_errors = 0;
	retval = yyparse();
	fclose(infile);

	lex_tini();
	if (cur_listen != NULL)
		free_listen_block(cur_listen);
	if (cur_pda != NULL)
		free_pda_block(cur_pda);
	if (cur_conduit != NULL)
		free_conduit_block(cur_conduit);

	if (retval > 0)
		return -retval;
	else if (num_errors > 0)
		return -1;
	else
		return 0;
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
