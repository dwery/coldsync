/* parser.h
 * Structures and such used by the config file parser.
 *
 *	Copyright (C) 1999-2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: parser.h,v 2.11 2001-10-12 02:22:29 arensb Exp $
 */
#ifndef _parser_h_
#define _parser_h_

#include <stdio.h>
#include <sys/param.h>		/* For MAXPATHLEN */
#include "coldsync.h"
#include "pconn/pconn.h"	/* For Palm types */
#include "conduit.h"

/* Start states.
 * See lex_expect() in "lexer.l".
 */
typedef enum {
	LEX_NONE = 100,		/* Normal starting state */
	LEX_HEADER,		/* Conduit argument header name */
	LEX_BSTRING,		/* "Bareword" (unquoted) string */
	LEX_CTPAIR,		/* Creator/type pair */
	LEX_ID4			/* 4-character identifier */
} lex_state_t;

/* crea_type_pair
 * A convenience struct that holds a creator-type pair, as long ints.
 */
typedef struct {
	udword creator;
	udword type;
} crea_type_pair;

extern int parse_trace;		/* Debugging level for config file parser */
extern int lineno;		/* Line number */

extern void lex_expect(const lex_state_t state);
extern void lex_tini(void);	/* Lexer cleanup */

#endif	/* _parser_h_ */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
