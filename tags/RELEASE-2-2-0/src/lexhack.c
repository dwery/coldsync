/* lexhack.c
 *
 * Stupid portability hack
 *
 *	Copyright (C) 2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: lexhack.c,v 2.1 2001-01-30 08:09:20 arensb Exp $
 */
/* This is a gross hack. The file created by 'flex' contains
 *	#include "config.h"
 * but not as the first thing in the file.
 * Since "config.h" includes some #defines and such to make things
 * work smoothly under various OSes, "lex.yy.c" doesn't always have
 * these symbols defined at the right time, so the compiler complains.
 * This hack forces "config.h" to be included first.
 */

#include "config.h"
#include "lex.yy.c"
