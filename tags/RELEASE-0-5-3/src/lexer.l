%{ /* -*- C -*- */
/* lexer.l
 * Lexer for config file parser.
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: lexer.l,v 2.3 1999-11-10 06:47:39 arensb Exp $
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include "parser.h"
#include "y.tab.h"

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

int lineno;
%}

 /* Decimal digit */
DIGIT		[0-9]
 /* Octal digit */
ODIGIT		[0-7]
/* Hex digit */
XDIGIT		[0-9a-fA-F]
 /* Whitespace */
WS		[ \t\f\r]

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
"speed"		{ return SPEED; }
"type"		{ return TYPE; }

 /* Conduit flavors */
"sync"		{ return SYNC; }
"fetch"		{ return FETCH; }
"pre-fetch"	{ return FETCH; /* Synonym */}
"dump"		{ return DUMP; }
"post-dump"	{ return DUMP; /* Synonym */}
"install" 	{ return INSTALL; }
"uninstall" 	{ return UNINSTALL; }

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
\"([^\"]|\\\")*\"	{
	if ((yylval.string = malloc(yyleng-1)) == NULL)
	{
		fprintf(stderr, "Can't malloc copy of string\n");
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
.	{ return yytext[0]; }

%%
