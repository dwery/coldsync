/* net_compat.h
 *
 * Declarations for the compatibility functions defined in net_funcs.c
 *
 *	Copyright (C) 2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: net_compat.h,v 2.1 2000-12-23 11:28:28 arensb Exp $
 */
#include "config.h"
#include <netdb.h>

#if !HAVE_INET_NTOP
extern const char *inet_ntop(int af, const void *src, char *dst, size_t size);
#endif	/* HAVE_INET_NTOP */

#if !HAVE_INET_PTON
extern int inet_pton(int af, const char *src, void *dst);
#endif	/* HAVE_INET_NTOP */
