/* parser.h
 * Structures and such used by the config file parser.
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: parser.h,v 2.2 1999-11-04 10:59:27 arensb Exp $
 */
#ifndef _parser_h_
#define _parser_h_

#include <stdio.h>
#include <sys/param.h>		/* For MAXPATHLEN */
#include "coldsync.h"
#include "pconn/pconn.h"	/* For Palm types */

extern int lineno;		/* Line number */

#endif	/* _parser_h_ */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
