/* parser.h
 * Structures and such used by the config file parser.
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: parser.h,v 2.1 1999-10-23 04:45:23 arensb Exp $
 */
#ifndef _parser_h_
#define _parser_h_

#include <stdio.h>
#include <sys/param.h>		/* For MAXPATHLEN */

/* listen_block
 * The information specified in a 'listen' block: which device to
 * listen on, and so forth.
 */
struct listen_block
{
	char device[MAXPATHLEN];	/* Device to listen on */
	int speed;			/* How fast to sync */
};

extern FILE *yyin;
extern FILE *yyout;			/* XXX - Is this useful? */

extern int yyparse(void);
extern int handle_listen(const struct listen_block *block);

#endif	/* _parser_h_ */
