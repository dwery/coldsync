%{ /* -*- C -*- */
/* lexer.l
 * Lexer for config file parser.
 *
 *	Copyright (C) 1999, 2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: lexer.l,v 2.22 2000-12-08 06:31:55 arensb Exp $
 */
#include "config.h"
#include <stdio.h>
#include <string.h>

#if HAVE_LIBINTL_H
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL_H */

#include "parser.h"
#include "y.tab.h"

#define PARSE_TRACE(n)	if (parse_trace >= (n))

#define KEYWORD(k) \
	PARSE_TRACE(3) \
		fprintf(stderr, "Found keyword " #k "\n"); \
	return k;

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

/* XXX - Is this hack still necessary? */
/* Hack for Linux: "../config.h" contains magic to that makes the
 * declaration of strdup() be included, but "../config.h" is included only
 * after <stdio.h>, which includes <features.h>, which controls whether
 * strdup() is declared :-(
 */
extern char *strdup(const char *string);

int lineno;

static Bool have_nextstate = False;
static int nextstate = 0;	/* State in which to start the next yylex()
				 * call (see lex_expect(), below).
				 */
%}

 /* We don't use yywrap(), and this option makes
  * things compile cleanly under Irix.
  * XXX - This appears to be a 'flex'-ism, and makes non-flex lexes choke.
  */
%option noyywrap

 /* Start states */

 /* Looking for a header name */
%s HEADER

 /* Looking for a "bareword" string: a string that isn't delimited by
  * quotation marks.
  */
%s BSTRING

 /* Creator/type pair (two four-character words with a slash in between) */
%s CTPAIR

 /* Four-character identifier */
%s ID4

 /* Character classes */

 /* Decimal digit */
DIGIT		[0-9]
 /* Octal digit */
ODIGIT		[0-7]
/* Hex digit */
XDIGIT		[0-9a-fA-F]
 /* Whitespace */
WS		[ \t\f\r]
 /* Characters allowed in "bareword" strings */
BWORD		[^ \t\f\n\r;:{}\"]
 /* Alphanumeric character */
ALNUM		[a-zA-Z0-9]
 /* Leading character for an identifier (anything that looks like a word) */
ID1		[a-zA-Z_]
 /* Subsequent characters for an identifier */
ID		[-a-zA-Z_0-9]

%%

%{
#ifdef YY_FLEX_LEX_COMPAT
	/* This is just to trick the compiler into thinking that this
	 * is used, so it'll shut up.
	 */
	if (0) goto find_rule;
#endif	/* YY_FLEX_LEX_COMPAT */

	/* See if the parser has given a hint about what to look for. See
	 * lex_expect(), below.
	 */
	if (have_nextstate)
	{
		PARSE_TRACE(7)
			fprintf(stderr, "lexer: Starting state %d\n",
				nextstate);
		BEGIN nextstate;
		have_nextstate = False;
	}
%}

<HEADER>{ID1}{ID}*	{
	PARSE_TRACE(3)
		fprintf(stderr, "Found header name [%s]\n", yytext);
	if ((yylval.string = strdup(yytext)) == NULL)
	{
		fprintf(stderr, _("%s: Can't make copy of string.\n"),
			"yylex");
		return -1;
	}
	BEGIN 0;
	return STRING;
	}

<BSTRING>{BWORD}+	{
	PARSE_TRACE(3)
		fprintf(stderr, "Found bareword string [%s]\n", yytext);
	if ((yylval.string = strdup(yytext)) == NULL)
	{
		fprintf(stderr, _("%s: Can't make copy of string.\n"),
			"yylex");
		return -1;
	}
	BEGIN 0;
	return STRING;
	}

 /* Ignore comments */
#.*		;

 /* Ignore whitespace */
{WS}+		;

 /* Ignore newlines, except to bump the line counter */
\n		{ lineno++; }

 /* Keywords */
"arguments"	{ KEYWORD(ARGUMENTS);	}
"conduit"	{ KEYWORD(CONDUIT);	}
"device"	{ KEYWORD(DEVICE);	}
"directory"	{ KEYWORD(DIRECTORY);	}
"listen"	{ KEYWORD(LISTEN);	}
"net"		{ KEYWORD(NET);	}
"network"	{ KEYWORD(NET);		/* Synonym */	}
"path"		{ KEYWORD(PATH);	}
"pda"		{ KEYWORD(PDA);		}
"palm"		{ KEYWORD(PDA);		/* Synonym */	}
"preference"	{ KEYWORD(PREFERENCE);	}
"pref"		{ KEYWORD(PREFERENCE);	/* Synonym */	}
"serial"	{ KEYWORD(SERIAL);	}
"usb"		{ KEYWORD(USB);		}
"saved"		{ KEYWORD(SAVED);	}
"snum"		{ KEYWORD(SNUM);	}
"speed"		{ KEYWORD(SPEED);	}
"type"		{ KEYWORD(TYPE);	}
"unsaved"	{ KEYWORD(UNSAVED);	}
"userid"	{ KEYWORD(USERID);	}
"username"	{ KEYWORD(USERNAME);	}
"Chosen"	{ KEYWORD(SAVED);	}
"Heathen"	{ KEYWORD(UNSAVED);	}

 /* Conduit flavors */
"sync"		{ KEYWORD(SYNC);	}
"fetch"		{ KEYWORD(FETCH);	}
"pre-fetch"	{ KEYWORD(FETCH);	/* Synonym */	}
"dump"		{ KEYWORD(DUMP);	}
"post-dump"	{ KEYWORD(DUMP);	/* Synonym */	}
"install"	{ KEYWORD(INSTALL);	}

 /* Conduit options */
"final"		{ KEYWORD(FINAL);	}
"default"	{ KEYWORD(DEFAULT);	}

 /* Conduit creator-type pairs. There are four rules for this, for
  * simplicity: "cccc/tttt", "cccc / *", "* / tttt", and "* / *" (without
  * the spaces, though).
  * These allow the user to specify a creator/type pair without having
  * double quotes all over the place. However, this will come back to bite
  * us on the ass if ``xxxx/yyyy'' is ever allowable in a different
  * context, e.g., if relative conduit pathnames without quotes become
  * acceptable, and the user decides to specify ``path quux/cond;''.
  */
<CTPAIR>{ALNUM}{4}"/"{ALNUM}{4} {
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

<CTPAIR>{ALNUM}{4}"/*" {
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

<CTPAIR>"*/"{ALNUM}{4} {
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

<CTPAIR>"*/*" {
	PARSE_TRACE(5)
		fprintf(stderr, "(lex) Found CREA_TYPE [%s]\n", yytext);

	yylval.crea_type.creator = 0L;
	yylval.crea_type.type = 0L;
	return CREA_TYPE;
	}

<ID4>{ALNUM}{4} {
	PARSE_TRACE(5)
		fprintf(stderr, "(lex) Found ID4 [%s]\n", yytext);

	if ((yylval.string = strdup(yytext)) == NULL)
	{
		fprintf(stderr, _("%s: Can't make copy of string.\n"),
			"yylex");
		return -1;
	}
	return STRING;
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
	PARSE_TRACE(3)
		fprintf(stderr, "Found string [%s]\n", yytext);
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
		long value;

		PARSE_TRACE(3)
			fprintf(stderr, "Found number [%s]\n", yytext);
		sscanf(yytext, "%li", &value);
		yylval.integer = value;
		return NUMBER;
	}

[-+]?0{ODIGIT}{1,11}	{
		long value;

		PARSE_TRACE(3)
			fprintf(stderr, "Found number [%s]\n", yytext);
		sscanf(yytext, "%li", &value);
		yylval.integer = value;
		return NUMBER;
	}

[-+]?0x{XDIGIT}{1,8}	{
		long value;

		PARSE_TRACE(3)
			fprintf(stderr, "Found number [%s]\n", yytext);
		sscanf(yytext, "%li", &value);
		yylval.integer = value;
		return NUMBER;
	}

 /* This isn't actually used, except in error-reporting: a WORD isn't
  * actually used in any parse rules, but yacc prints the bogus token that
  * caused the error. In this case, it's better to print an entire word,
  * rather than just the first character that caused the error.
  */
{ID1}{ID}*	{
	PARSE_TRACE(3)
		fprintf(stderr, "Found word [%s]\n", yytext);

	if ((yylval.string = strdup(yytext)) == NULL)
	{
		fprintf(stderr, _("%s: Can't make copy of string.\n"),
			"yylex");
		return -1;
	}

	return WORD;
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

void
lex_expect(const int state)
{
	PARSE_TRACE(5)
		fprintf(stderr, "Lex: expecting state %d\n", state);

	switch (state)
	{
	    case 0:
		nextstate = 0;
		break;
	    case LEX_HEADER:
		nextstate = HEADER;
		break;
	    case LEX_BSTRING:
		nextstate = BSTRING;
		break;
	    case LEX_CTPAIR:
		nextstate = CTPAIR;
		break;
	    case LEX_ID4:
		nextstate = ID4;
		break;
	    default:
		fprintf(stderr, _("%s: unknown start state %d\n"
			"Please tell the maintainer to fix `lexer.l'.\n"),
			"lex_expect", state);
	}
	have_nextstate = True;
}

/* This is for Emacs's benefit:
 * Local Variables:	***
 * fill-column:	75	***
 * End:			***
 */
