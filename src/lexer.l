%{ /* -*- C -*- */
/* lexer.l
 * Lexer for config file parser.
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: lexer.l,v 2.1 1999-10-23 04:45:03 arensb Exp $
 */
#include "config.h"
#include <string.h>
#include "parser.h"
#include "y.tab.h"

char mystring[2048];	/* XXX - For testing only */
%}

 /* Decimal digit */
DIGIT		[0-9]
 /* Octal digit */
ODIGIT		[0-7]
/* Hex digit */
XDIGIT		[0-9a-fA-F]
 /* Whitespace */
WS		[ \t\f\r\n]

%%

 /* Ignore comments */
#.*\n		;

 /* Ignore whitespace */
{WS}+		;

 /* Keywords */
"conduit"	{ return CONDUIT; }
"listen"	{ return LISTEN; }
"name"		{ return NAME; }
"path"		{ return PATH; }
"serial"	{ return SERIAL; }
"speed"		{ return SPEED; }
"type"		{ return TYPE; }

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
	strncpy(mystring, yytext+1, yyleng-2);
	mystring[yyleng-2] = '\0';
	yylval.string = mystring;
	fprintf(stderr, "<<<string: [%s] (%d) {%d}\n",
		yytext, yyleng, strlen(yytext));
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
