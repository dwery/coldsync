/* parser.h
 * Structures and such used by the config file parser.
 *
 *	Copyright (C) 1999-2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: parser.h,v 2.9 2001-01-09 16:28:14 arensb Exp $
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
#define LEX_HEADER	1	/* Conduit argument header name */
#define LEX_BSTRING	2	/* "Bareword" (unquoted) string */
#define LEX_CTPAIR	3	/* Creator/type pair */
#define LEX_ID4		4	/* 4-character identifier */

/* crea_type_pair
 * A convenience struct that holds a creator-type pair, as long ints.
 */
typedef struct {
	udword creator;
	udword type;
} crea_type_pair;

extern int parse_trace;		/* Debugging level for config file parser */
extern int lineno;		/* Line number */

extern void lex_expect(const int state);

#endif	/* _parser_h_ */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
