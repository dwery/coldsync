/* net_compat.h
 *
 * Declarations for the compatibility functions defined in net_funcs.c
 *
 *	Copyright (C) 2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: net_compat.h,v 2.2 2001-01-30 08:13:35 arensb Exp $
 */
#include "config.h"
#include <netdb.h>

#if !HAVE_INET_NTOP
extern const char *inet_ntop(int af, const void *src, char *dst, size_t size);
#endif	/* HAVE_INET_NTOP */

#if !HAVE_INET_PTON
extern int inet_pton(int af, const char *src, void *dst);
#endif	/* HAVE_INET_NTOP */

/* Maximum length of the printed representation of an IPv4 address */
#ifndef INET_ADDRSTRLEN
#  define INET_ADDRSTRLEN	16
#endif	/* INET_ADDRSTRLEN */

/* Maximum length of the printed representation of an IPv6 address */
#ifndef INET6_ADDRSTRLEN
#  define INET6_ADDRSTRLEN	46
#endif	/* INET6_ADDRSTRLEN */

/* This is for Emacs's benefit:
 * Local Variables:	***
 * mode: C		***
 * fill-column:	75	***
 * End:			***
 */
