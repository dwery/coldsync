/* parser.h
 * Structures and such used by the config file parser.
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: parser.h,v 2.4 2000-02-07 04:43:24 arensb Exp $
 */
#ifndef _parser_h_
#define _parser_h_

#include <stdio.h>
#include <sys/param.h>		/* For MAXPATHLEN */
#include "coldsync.h"
#include "pconn/pconn.h"	/* For Palm types */

/* crea_type_pair
 * A convenience struct that holds a creator-type pair, as long ints.
 */
typedef struct {
	udword creator;
	udword type;
} crea_type_pair;

extern int parse_trace;		/* Debugging level for config file parser */
extern int lineno;		/* Line number */

#endif	/* _parser_h_ */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */