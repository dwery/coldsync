%{ /* -*- C -*- */
/* lexer.l
 * Lexer for config file parser.
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: lexer.l,v 2.41 2001-11-12 05:50:38 arensb Exp $
 */
#include <stdio.h>
#include <string.h>

#if HAVE_LIBINTL_H
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL_H */

#include "parser.h"
#include "symboltable.h"
#include "y.tab.h"

#define PARSE_TRACE(n)	if (parse_trace >= (n))

#define KEYWORD(k) \
	PARSE_TRACE(3) \
		fprintf(stderr, "Found keyword " #k "\n"); \
	return k;

#undef YY_USES_REJECT	/* This makes the compiler shut up */
#ifdef ECHO
#undef ECHO		/* <termios.h> also defines ECHO */
#endif	/* ECHO */

static void qstr_clear(void);	/* Clear contents of 'qstring' */
static int qstr_append(const char *data, const int len);

int lineno;

static Bool have_nextstate = False;
static int nextstate = 0;	/* State in which to start the next yylex()
				 * call (see lex_expect(), below).
				 */

/* These variables help implement featureful double-quoted strings in
 * <QSTRING> context.
 */
static char *qstring = NULL;	/* The string itself */
static int qstr_startline = 0;	/* Line on which current qstring started */
static int qstr_len = 0;	/* # of interesting characters in
				 * 'qstring', not counting the terminating
				 * NUL. */
static int qstr_size = 0;	/* Amount of memory used by 'qstring' */
%}

 /* We don't use yywrap(), and this option makes
  * things compile cleanly under Irix.
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

 /* Variable name */
%s VARNAME

/* Quoted strings. */
%x QSTRING

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
 /* Leading character for a variable name */
VAR1		[a-zA-Z_]
 /* Subsequent characters for a variable name */
VAR		[a-zA-Z_0-9]
 /* Ordinary characters in double-quoted strings */
QSTR		[^\\\$\"\n]

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
		Error(_("%s: Can't make copy of string."),
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
		Error(_("%s: Can't make copy of string."),
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
"force_install"	{ KEYWORD(FORCE_INSTALL);	}
"forward"	{ KEYWORD(FORWARD);	}
"install_first"	{ KEYWORD(INSTALL_FIRST);	}
"listen"	{ KEYWORD(LISTEN);	}
"options"	{ KEYWORD(OPTIONS);	}
"path"		{ KEYWORD(PATH);	}
"pda"		{ KEYWORD(PDA);		}
"palm"		{ KEYWORD(PDA);		/* Synonym */	}
"preference"	{ KEYWORD(PREFERENCE);	}
"pref"		{ KEYWORD(PREFERENCE);	/* Synonym */	}
"protocol"	{ KEYWORD(PROTOCOL);	}
"saved"		{ KEYWORD(SAVED);	}
"snum"		{ KEYWORD(SNUM);	}
"speed"		{ KEYWORD(SPEED);	}
"transient"	{ KEYWORD(TRANSIENT);	}
"type"		{ KEYWORD(TYPE);	}
"unsaved"	{ KEYWORD(UNSAVED);	}
"userid"	{ KEYWORD(USERID);	}
"username"	{ KEYWORD(USERNAME);	}
"Chosen"	{ KEYWORD(SAVED);	}
"Heathen"	{ KEYWORD(UNSAVED);	}

 /* Boolean values */
[Tt]"rue"	{ KEYWORD(TRUE);	}
[Yy]"es"	{ KEYWORD(TRUE);	/* Synonym */	}
[Ff]"alse"	{ KEYWORD(FALSE);	}
[Nn]"o"		{ KEYWORD(FALSE);	/* Synonym */	}

 /* Hardware protocols */
"net"		{ KEYWORD(NET);	}
"network"	{ KEYWORD(NET);		/* Synonym */	}
"serial"	{ KEYWORD(SERIAL);	}
"usb"		{ KEYWORD(USB);		}

 /* Software protocols */
 /* "default" already in conduit options */
"full"		{ KEYWORD(FULL);	}
"simple"	{ KEYWORD(SIMPLE);	}
 /* "net" already in keywords */

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

<VARNAME>{VAR1}{VAR}*	{
	PARSE_TRACE(3)
		fprintf(stderr, "Found variable name [%s]\n", yytext);
	if ((yylval.string = strdup(yytext)) == NULL)
	{
		Error(_("%s: Can't make copy of string."),
		      "yylex");
		return -1;
	}
	BEGIN 0;
	return STRING;
	}

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
		Error(_("%s: Can't make copy of string."),
		      "yylex");
		return -1;
	}
	return STRING;
	}

 /* XXX - How does iconv() figure into all this? It'd be nice to allow
  * Japanese users to enter strings in Japanese (EUC?). With any luck, the
  * various committees were smart and defined encodings that wouldn't
  * horribly break legacy C code (like this).
  * The main things to watch out for, AFAICT are:
  *
  * 1) Are non-ASCII characters (>= 0x80) even handled properly by 'flex'?
  *
  * 2) Stray NULs: if 0x1200 maps to some significant character in some
  * language, the 0x00 will be interpreted as a terminating NUL in many
  * places. This should probably be handled here.
  *
  * 3) "Puns": If the character 0x2233 is a real character in some
  * character set, the initial 0x22 might be interpreted as a double-quote
  * (since '"' == 0x22).
  *
  * With any luck, the various committees were smart enough to foresee and
  * avoid these problems.
  * If other character sets are compatible with plain ASCII, and allow us
  * to process things "naively", then presumably the Right Thing to do is
  * to call iconv() at the end.
  */
 /* XXX - It would be intuitively good if "bare" variables were recognized.
  * That is, currently you can have:
  *	SomeOption: "$(VAR)";
  * but not
  *	SomeOption: $(VAR);
  * Presumably a "bare" variable should be parsed as if it were a
  * double-quoted string by itself.
  * What about conduit arguments? Ought to do the intuitive thing for
  *	conduit dump {
  *		...
  *	    arguments:
  *		$(VARIABLE): $(VALUE);
  *	}
  * What about
  *	options {
  *		PATH: "path";
  *	}
  *	conduit dump {
  *		$(PATH): /path/to/conduit
  *	}
  * Should this work? Seems like it should, but it'd be a bitch to parse.
  */
 /* Note that this accepts escaped double-quotes in strings */
\"{QSTR}* {
	int err;

	PARSE_TRACE(5)
		fprintf(stderr, "Started string [%s]\n", yytext);

	qstr_clear();		/* Erase previous qstring, if any */

	/* Append the current text */
	if ((err = qstr_append(yytext+1, yyleng-1)) < 0)
	{
		Error(_("%s: Can't append to qstring."),
		      "yylex");
		return -1;
	}

	BEGIN QSTRING;
        }

 /* Octal character: \0123 */
<QSTRING>\\0{ODIGIT}{1,3} {
	int char_value;
	char buf[2];		/* Dummy buffer, for qstr_append() */

	sscanf(yytext+2, "%3o", &char_value);
	buf[0] = (char_value & 0xff);
	buf[1] = '\0';
	qstr_append(buf, 1);
	}

 /* Hex characters: \xf0 */
 /* XXX - What about wide characters? IIRC C9x and Perl allow \x1234 for
  * wide characters. It'd be nice to support this, but it'd make sense to
  * convert entirely to wide characters internally first.
  */
<QSTRING>\\x{XDIGIT}{2} {
	int char_value;
	char buf[2];		/* Dummy buffer, for qstr_append */

	sscanf(yytext+2, "%2x", &char_value);
	buf[0] = (char_value & 0xff);
	buf[1] = '\0';
	qstr_append(buf, 1);
	}

 /* escaped characters: $, \", \n, \t, \r.
  */
<QSTRING>\\(.|\n) {
	PARSE_TRACE(5)
		fprintf(stderr, "escaped string character [%s]\n", yytext);
	switch (yytext[1]) {
		/* These are the same special characters as in C. */
	    case 'a':			/* BEL */
		qstr_append("\a", 1);
		break;
	    case 'b':			/* Backspace */
		qstr_append("\b", 1);
		break;
	    case '\f':			/* Form feed */
		qstr_append("\f", 1);
		break;
	    case 'n':			/* Newline */
		qstr_append("\n", 1);
		break;
	    case 'r':			/* Carriage return */
		qstr_append("\r", 1);
		break;
	    case 't':			/* Tab */
		qstr_append("\t", 1);
		break;
	    case 'v':			/* Vertical tab */
		qstr_append("\v", 1);
		break;
	    case '\n':
		qstr_append("\n", 1);
		lineno++;
		break;
	    default:			/* Any other character is just
					 * itself */
		qstr_append(yytext+1, 1);
		break;
	}
	}

 /* Variables.
  * Note that the second character can be either '(' or '{', to allow both
  * $(VAR) and ${VAR}. The final character can be anything, even a newline.
  * This will be checked to make sure it's the right closing character. If
  * it isn't, print an error message.
  */
<QSTRING>\$[\(\{]{VAR1}{VAR}*(.|\n) {
	char *value;		/* Variable value */

	PARSE_TRACE(5)
		fprintf(stderr, "variable [%s]\n", yytext);

	/* Make sure the variable substitution was closed, and with the
	 * character that matches the starting one.
	 */
	switch (yytext[yyleng-1]) {
	    case ')':
		if (yytext[1] != '(')
			/* XXX - It'd be nice to print the config file name
			 * here, but parser.y:conf_fname is static.
			 */
			Warn(_("%d: Unexpected character near \"%s\".\n"
			       "    You probably meant \"$(%.*s)\".\n"),
			     lineno, yytext, yyleng-3, yytext+2);
		break;
	    case '}':
		if (yytext[1] != '{')
			/* XXX - It'd be nice to print the config file name
			 * here, but parser.y:conf_fname is static.
			 */
			Warn(_("%d: Unexpected character near \"%s\".\n"
			       "    You probably meant \"${%.*s}\".\n"),
			     lineno, yytext, yyleng-3, yytext+2);
		break;
	    default:	/* Any other character */
		Error(_("%d: Unterminated variable reference near \"%s\".\n"
			"    You probably meant \"$(%.*s)\".\n"),
		      lineno, yytext, yyleng-3, yytext+2);
		yyless(yyleng-1);	/* Return last char to input */
	}

	/* Look up the variable */
	value = get_symbol_n(yytext+2, yyleng-3);

	PARSE_TRACE(5)
		fprintf(stderr, "$(%.*s) == \"%s\"\n",
			yyleng-3, yytext+2,
			(value == NULL ? "(nil)" : value));

	/* If the variable expands to the empty string, don't do anything.
	 * Otherwise, append its value to 'qstring'.
	 */
	if ((value != NULL) && (value[0] != '\0'))
		qstr_append(value, strlen(value));
	free(value);
        }

 /* String of ordinary characters */
<QSTRING>{QSTR}+ {
	PARSE_TRACE(5)
		fprintf(stderr, "more string [%s]\n", yytext);
	qstr_append(yytext, yyleng);
        }

 /* Unescaped newline */
<QSTRING>\n {
	Error(_("Unterminated string at line %d (string started "
		"on line %d)\n"),
	      lineno, qstr_startline);

	/* Pretend the string was terminated here */
	unput('\"');
	}

 /* End quote of string. */
<QSTRING>\" {
	BEGIN INITIAL;
	if ((yylval.string = strdup(qstring)) == NULL)
	{
		Error(_("%s: Can't make copy of string."),
		      "yylex");
		return -1;
	}
	PARSE_TRACE(2)
		fprintf(stderr, "finished string [%s]\n", yylval.string);
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
		Error(_("%s: Can't make copy of string."),
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

#if 0
/* XXX - This used to be the last rule, just a few lines above. It has
 * since been removed because it causes a segfault when you have both
 * /usr/local/etc/coldsync.conf and ~/.coldsyncrc . This may become an
 * issue again once it's possible to #include files.
 */
<<EOF>>	{
	/* XXX - This will break if and when .coldsyncrc can include other
	 * files. See flex(1), but bear in mind that its file-inclusion
	 * example leaks memory at the end of the outermost file (it calls
	 * yyterminate(), but not yy_delete_bufffer(YY_CURRENT_BUFFER);
	 */
	/* Free current buffer to avoid memory leak */
	yy_delete_buffer(YY_CURRENT_BUFFER);
	yyterminate();
	}
#endif

void
lex_expect(const lex_state_t state)
{
	PARSE_TRACE(5)
		fprintf(stderr, "Lex: expecting state %d\n", state);

	switch (state)
	{
	    case LEX_NONE:
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
	    case LEX_VAR:
		nextstate = VARNAME;
		break;
	    default:
		Error(_("%s: unknown start state %d.\n"
			"Please tell the maintainer to fix `lexer.l'."),
		      "lex_expect",
		      state);
	}
	have_nextstate = True;
}

void
lex_tini(void)
{
	/* Clean up 'qstring', if it was used */
	if (qstring != NULL)
	{
		free(qstring);
		qstring = NULL;
		qstr_len = 0;
		qstr_size = 0;
	}
}

/* qstr_clear
 * Clear the current contents of 'qstring'.
 */
static void
qstr_clear(void)
{
	if (qstring == NULL)
		return;
	qstring[0] = '\0';
	qstr_startline = lineno;
	qstr_len = 0;
	/* Don't change 'qstr_size', since we haven't freed any memory */
}

/* qstr_append
 * Append a string to 'qstring'. If necessary, 'qstring' is realloc()ed and
 * made larger. qstr_append() appends 'len' bytes from 'data' to 'qstring',
 * and keeps a NUL at the end of the string. That is, if you're appending
 * "foo", you should qstr_append("foo", 3). 'len' does not include the NUL.
 *
 * Returns 0 if successful, or a negative value in case of error.
 */
static int
qstr_append(const char *data,		/* Data to append */
	    const int len)		/* Length of 'data' */
{
	if (len <= 0)
		return 0;

	PARSE_TRACE(6)
		fprintf(stderr, "qstr_append(\"%.*s\", %d)\n",
			len, data, len);

	/* Increase the size of 'qstring', if necessary */
	if (qstr_len + len + 1 >= qstr_size)
	{
		char *newqstring;
		int newsize;

		if (qstr_size == 0)
		{
			/* First time around. Need to allocate a new string
			 * buffer. Make it big enough for the current
			 * string, rounded up to the nearest Kb.
			 */
			newsize = (len + 1 + 1023) & (~0x03ff);
			PARSE_TRACE(7)
				fprintf(stderr, "Creating initial qstring, "
					"newsize == %d\n",
					newsize);
			if ((newqstring = malloc(newsize)) == NULL)
			{
				Error(_("%s: Out of memory."),
					"qstr_append");
				Perror("malloc");
				return -1;
			}

			/* Initialize 'qstring' as an empty string. Its
			 * length is 0, and its size is 'newsize'.
			 */
			qstring = newqstring;
			qstring[0] = '\0';
			qstr_len = 0;
			qstr_size = newsize;
			PARSE_TRACE(7)
				fprintf(stderr,
					"Now qstring == [%s] len %d size %d\n",
					qstring, qstr_len, qstr_size);

		} else {
			/* Second through nth time around. 'qstring'
			 * already exists, but it's too small. Add enough
			 * for 'data', rounded up to the nearest Kb.
			 */
			newsize = (qstr_len + len + 1 + 1023) & (~0x03ff);
			if ((newqstring = realloc(qstring, newsize)) == NULL)
			{
				Error(_("%s: realloc(%d) failed."),
				      "qstr_append", newsize);
				Perror("realloc");
				return -1;
			}

			qstring = newqstring;
			qstr_size = newsize;
		}
	}

	/* Append the new string to 'qstring'. We use memcpy() and not
	 * strncpy() because 'data' might contain weird characters and
	 * NULs.
	 */
	memcpy(qstring+qstr_len, data, len);
	qstr_len += len;
	qstring[qstr_len] = '\0';

	PARSE_TRACE(7)
		fprintf(stderr, "Now qstring is [%.*s] len %d size %d\n",
			qstr_len, qstring, qstr_len, qstr_size);
	return 0;
}

/* This is for Emacs's benefit:
 * Local Variables:	***
 * fill-column:	75	***
 * End:			***
 */
