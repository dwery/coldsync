%{ /* -*- C -*- */
/* lexer.l
 * Lexer for config file parser.
 *
 *	Copyright (C) 1999, 2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: lexer.l,v 2.14 2000-05-19 12:12:44 arensb Exp $
 */
/* XXX - 'flex' by itself can (apparently) deal with infinitely-long
 * tokens. 'flex -l' (and presumably 'lex') can't. Figure out some way of
 * dealing with this, particularly wrt strings and user-supplied conduit
 * arguments. Perhaps replace ".*" with ".{0,255}" or some such.
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
 /* An error has occurred. Just return the rest of the line */
%s ERROR

 /* Looking for a conduit header */
%s HEADER

 /* Character classes */

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
 /* Leading character for an identifier */
ID1		[a-zA-Z_]
 /* Subsequent characters for an identifier */
ID		[a-zA-Z_0-9]
 /* Subsequent characters for a header identifier */
HID		[-a-zA-Z_0-9]

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

 /* XXX - Perhaps this should be rewritten to stop at a semicolon */
<ERROR>.*	{
	yylval.string = strdup(yytext);
	return STRING;
	}

<HEADER>^{WS}*{ID1}{HID}*:{WS}*.*	{
		/* It's actually easier to just recognize the entire line
		 * all at once and parse it here.
		 */
		/* XXX - The spec gives a maximum length for both the
		 * header name and header value. Deal with them here.
		 */
		char *hname;	/* Pointer to header name */
		char *hvalue;	/* Pointer to header value */
		char *colon;	/* Pointer to colon within the line */

		/* XXX - If line ends in "\", there's a continuation line.
		 * If it ends in ';', that's the end of the line.
		 * How to deal with whitespace at the beginning of the
		 * continuation line? Or would it be best to just bag the
		 * whole continuation line idea entirely?
		 */
		if (yytext[yyleng-1] == ';')
			/* Put the semicolon back on the input stream */
			yyless(yyleng-1);

		hname = yytext;
		hname += strspn(hname, " \t\f");
					/* Skip leading whitespace */

		/* Find the colon that separates the header name from the
		 * value.
		 */
		colon = strchr(hname, ':');
		*colon = '\0';		/* XXX - Ugly? */

		PARSE_TRACE(6)
			fprintf(stderr, "Header name: [%s]\n", hname);
		yylval.header_type.name = strdup(hname);
				/* Save the header name */
				/* XXX - Should truncate at
				 * COND_MAXHFIELDLEN characters.
				 */

		/* Find the header value after the colon */
		hvalue = colon + 1;
		hvalue += strspn(hvalue, " \t\f");

		PARSE_TRACE(6)
			fprintf(stderr, "Header value: [%s]\n", hvalue);

		/* XXX - If value begins and ends with double quotes, strip
		 * them.
		 */
		/* XXX - Process escaped characters */
		yylval.header_type.value = strdup(hvalue);
				/* Save the header value */
				/* XXX - Should truncate at COND_MAXLINELEN
				 * - COND_MAXHFIELDLEN-2 characters.
				 */

		return HEADER_PAIR;
	}

 /* XXX - This is really a hack. Need to redo the whole header thing */
<HEADER>"}"	{
	BEGIN 0;
	return yytext[0];
	}

 /* Ignore comments */
#.*		;

 /* Ignore whitespace */
{WS}+		;

 /* Ignore newlines, except to bump the line counter */
\n		{ lineno++; }

 /* Keywords */
"arguments"	{ return ARGUMENTS;	}
"conduit"	{ return CONDUIT;	}
"device"	{ return DEVICE;	}
"directory"	{ return DIRECTORY;	}
"listen"	{ return LISTEN;	}
"name"		{ return NAME;		}
"path"		{ return PATH;		}
"pda"		{ return PDA;		}
"palm"		{ return PDA; /* Synonym */	}
"serial"	{ return SERIAL;	}
"usb"		{ return USB;		}
"snum"		{ return SNUM;		}
"speed"		{ return SPEED;		}
"type"		{ return TYPE;		}

 /* Conduit flavors */
"sync"		{ return SYNC;		}
"fetch"		{ return FETCH;		}
"pre-fetch"	{ return FETCH;	/* Synonym */	}
"dump"		{ return DUMP;		}
"post-dump"	{ return DUMP;	/* Synonym */	}
"install" 	{ return INSTALL;		}
"uninstall" 	{ return UNINSTALL;	}

 /* Conduit options */
"final"		{ return FINAL;		}
"default"	{ return DEFAULT;	}

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

 /* XXX - Is this actually used anywhere? */
{ID1}{ID}*	{
		if ((yylval.string = malloc(yyleng+1)) == NULL)
		{
			fprintf(stderr, _("%s: Can't malloc copy of word\n"),
				"yylex");
			return -1;
		}
		strncpy(yylval.string, yytext, yyleng);
		yylval.string[yyleng] = '\0';
fprintf(stderr, "Returning WORD [%s]\n", yylval.string);
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
	switch (state)
	{
	    case 0:
		nextstate = 0;
		break;
	    case LEX_ERROR:
		nextstate = ERROR;
		break;
	    case LEX_HEADER:
		nextstate = HEADER;
		break;
	    default:
		fprintf(stderr, _("%s: unknown start state %d\n"),
			"lex_expect", state);
	}
	have_nextstate = True;
}

/* This is for Emacs's benefit:
 * Local Variables:	***
 * fill-column:	75	***
 * End:			***
 */
