%{ /* -*- C -*- */
/* lexer.l
 * Lexer for config file parser.
 *
 *	Copyright (C) 1999, 2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: lexer.l,v 2.10 2000-02-07 04:43:09 arensb Exp $
 */
#include "config.h"
#include <stdio.h>
#include <string.h>

#if HAVE_LIBINTL
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL */

#include "parser.h"
#include "y.tab.h"

#define PARSE_TRACE(n)	if (parse_trace >= (n))

#define YY_NO_UNPUT	/* We never yyunput() anything, so this keeps
			 * the compiler from complaining.
			 */
#undef YY_USES_REJECT	/* This makes the compiler shut up */
#undef YY_NEED_STRLEN	/* This makes the compiler shut up */
			/* XXX - This might need to be taken out when
			 * strings are better developed: this
			 * basically says that the lexer isn't using
			 * yymore() or yyless(); these might be useful
			 * with strings.
			 */
#ifdef ECHO
#undef ECHO		/* <termios.h> also defines ECHO */
#endif	/* ECHO */

/* This is a hack for Solaris. It requires __EXTENSIONS__ or
 * _POSIX_C_SOURCE to be defined in order to find the declaration for
 * fileno(). However, lint defines fileno() as a cpp macro, hence the
 * #ifndef block.
 */
#ifndef fileno
extern int fileno(FILE *);
#endif  /* fileno */

int lineno;
%}

 /* We don't use yywrap(), and this option makes
  * things compile cleanly under Irix.
  */
%option noyywrap

 /* Decimal digit */
DIGIT		[0-9]
 /* Octal digit */
ODIGIT		[0-7]
/* Hex digit */
XDIGIT		[0-9a-fA-F]
 /* Whitespace */
WS		[ \t\f\r]
 /* Alphanumeric character */
ALNUM		[a-zA-Z0-9]

%%

%{
#ifdef YY_FLEX_LEX_COMPAT
	/* This is just to trick the compiler into thinking that this
	 * is used, so it'll shut up.
	 */
	if (0) goto find_rule;
#endif	/* YY_FLEX_LEX_COMPAT */
%}

 /* Ignore comments */
#.*		;

 /* Ignore whitespace */
{WS}+		;

 /* Ignore newlines, except to bump the line counter */
\n		{ lineno++; }

 /* Keywords */
"conduit"	{ return CONDUIT; }
"device"	{ return DEVICE; }
"listen"	{ return LISTEN; }
"name"		{ return NAME; }
"path"		{ return PATH; }
"serial"	{ return SERIAL; }
"usb"		{ return USB; }
"speed"		{ return SPEED; }
"type"		{ return TYPE; }

 /* Conduit flavors */
"sync"		{ return SYNC; }
"fetch"		{ return FETCH; }
"pre-fetch"	{ return FETCH;	/* Synonym */}
"dump"		{ return DUMP; }
"post-dump"	{ return DUMP;	/* Synonym */}
"install" 	{ return INSTALL; }
"uninstall" 	{ return UNINSTALL; }

 /* Conduit options */
 /* XXX - Implement in parser. Add flags in conduit descriptor. Make
  * the run_*_conduits() functions check for these.
  */
"final"		{ return FINAL; }
"default"	{ return DEFAULT; }

 /* Conduit creator-type pairs. There are four rules for this, for
  * simplicity: "cccc/tttt", "cccc / *", "* / tttt", and "* / *" (without
  * the spaces, though).
  * These allow the user to specify a creator/type pair without having
  * double quotes all over the place. However, this will come back to bite
  * us on the ass if ``xxxx/yyyy'' is ever allowable in a different
  * context, e.g., if relative conduit pathnames without quotes become
  * acceptable, and the user decides to specify ``path quux/cond;''.
  */
{ALNUM}{4}"/"{ALNUM}{4} {
	PARSE_TRACE(5)
		fprintf(stderr, "(lex) Found CREA_TYPE [%s]\n", yytext);

	yylval.crea_type.creator =
		(yytext[0] << 24) |
		(yytext[1] << 16) |
		(yytext[2] <<  8) |
		yytext[3];
	yylval.crea_type.type =
		(yytext[5] << 24) |
		(yytext[6] << 16) |
		(yytext[7] <<  8) |
		yytext[8];
	return CREA_TYPE;
	}

{ALNUM}{4}"/*" {
	PARSE_TRACE(5)
		fprintf(stderr, "(lex) Found CREA_TYPE [%s]\n", yytext);

	yylval.crea_type.creator =
		(yytext[0] << 24) |
		(yytext[1] << 16) |
		(yytext[2] <<  8) |
		yytext[3];
	yylval.crea_type.type = 0L;
	return CREA_TYPE;
	}

"*/"{ALNUM}{4} {
	PARSE_TRACE(5)
		fprintf(stderr, "(lex) Found CREA_TYPE [%s]\n", yytext);

	yylval.crea_type.creator = 0L;
	yylval.crea_type.type =
		(yytext[5] << 24) |
		(yytext[6] << 16) |
		(yytext[7] <<  8) |
		yytext[8];
	return CREA_TYPE;
	}

"*/*" {
	PARSE_TRACE(5)
		fprintf(stderr, "(lex) Found CREA_TYPE [%s]\n", yytext);

	yylval.crea_type.creator = 0L;
	yylval.crea_type.type = 0L;
	return CREA_TYPE;
	}

 /* XXX - This should be capable of handling strings longer than
  * YYLMAX. The obvious way to do this is to implement a "string"
  * context: the first double-quote puts us in string mode; the second
  * one takes us out of it. In the meantime, we read YYLMAX-sized
  * chunks of the string, and either a) realloc() storage for the
  * string as necessary, or b) put the chunks on a linked list, then
  * collapse them into a single string once the closing double-quotes
  * have been seen.
  *
  * Alternately, just require 'flex' rather than 'lex'.
  */
 /* Note that this accepts escaped double-quotes in strings */
 /* XXX - Allow other special characters: \t, \n, \r, \0123, \xf3 and
  * so forth.
  */
 /* XXX - Strip out escapes in strings */
\"([^\"]|\\\")*\"	{
	if ((yylval.string = malloc(yyleng-1)) == NULL)
	{
		fprintf(stderr, _("%s: Can't malloc copy of string\n"),
			"yylex");
		return -1;
	}
	strncpy(yylval.string, yytext+1, yyleng-2);
	yylval.string[yyleng-2] = '\0';
	return STRING; 
	}

[-+]?{DIGIT}{1,10}	{
		int value;
		sscanf(yytext, "%i", &value);
		yylval.integer = value;
		return NUMBER;
	}

[-+]?0{ODIGIT}{1,11}	{
		int value;
		sscanf(yytext, "%i", &value);
		yylval.integer = value;
		return NUMBER;
	}

[-+]?0x{XDIGIT}{1,8}	{
		int value;
		sscanf(yytext, "%i", &value);
		yylval.integer = value;
		return NUMBER;
	}

 /* Anything else, just return it. */
.	{
		PARSE_TRACE(7)
			fprintf(stderr,
				"(lex) Found none of the above: [%s]\n",
				yytext);
		return yytext[0];
	}

%%

/* This is for Emacs's benefit:
 * Local Variables:	***
 * fill-column:	75	***
 * End:			***
 */
