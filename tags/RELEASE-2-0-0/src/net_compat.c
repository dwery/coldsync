/* net_compat.c
 *
 * Various network-related functions, included for compatibility.
 * These functions only support IPv4, on the assumption that if the OS
 * doesn't have them, it doesn't support IPv6.
 *
 *	Copyright (C) 2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: net_compat.c,v 2.2 2001-01-30 08:13:00 arensb Exp $
 */
#include "config.h"
#include <stdio.h>		/* For NULL */
#include <string.h>		/* For strlen() and friends */
#include <errno.h>		/* For EAFNOSUPPORT */
#include <sys/types.h>		/* For inet_aton() */
#include <sys/socket.h>		/* For inet_aton() */
#include <netinet/in.h>		/* For inet_aton(), INET_ADDRSTRLEN */
#include <arpa/inet.h>		/* For inet_aton() */
#include "net_compat.h"

#if !HAVE_SNPRINTF
extern int snprintf(char *buf, size_t len, const char *format, ...);
#endif	/* HAVE_SNPRINTF */

#if !HAVE_INET_NTOP
/* inet_ntop
 * Compatibility replacement for inet_ntop().
 * This function is heavily inspired by the one in W. Richard Stevens,
 * "Unix Network Programming", vol. 1, section 3.7, fig. 3.12.
 */
const char *
inet_ntop(int af, const void *src, char *dst, size_t size)
{
	const unsigned char *p = (const unsigned char *) src;
	char temp[INET_ADDRSTRLEN];

	/* Check address family.
	 * Wimpy, inferior OSes only get IPv4.
	 */
	if (af != AF_INET)
	{
		errno = EAFNOSUPPORT;	/* Unsupported address family */
		return NULL;
	}

	/* Write the string */
	snprintf(temp, sizeof(temp), "%d.%d.%d.%d",
		 p[0], p[1], p[2], p[3]);

	/* Make sure the printed string is sane. */
	if (strlen(temp) >= size)
	{
		errno = ENOSPC;		/* String is too big to fit in
					 * buffer */
		return NULL;
	}

	/* Copy the string to the caller's buffer */
	strcpy(dst, temp);
	return dst;
}
#endif	/* HAVE_INET_NTOP */

#if !HAVE_INET_PTON
/* inet_ntop
 * Compatibility replacement for inet_ntop().
 * This function is heavily inspired by the one in W. Richard Stevens,
 * "Unix Network Programming", vol. 1, section 3.7, fig. 3.11.
 */
int
inet_pton(int af, const char *src, void *dst)
{
	struct in_addr in_val;

	/* Check address family.
	 * Wimpy, inferior OSes only get IPv4.
	 */
	if (af != AF_INET)
	{
		errno = EAFNOSUPPORT;	/* Unsupported address family */
		return -1;
	}

	/* Convert the string to an address */
	if (inet_aton(src, &in_val) != 1)
		return 0;		/* Failure */

	/* Copy the address to caller */
	memcpy(dst, &in_val, sizeof(struct in_addr));

	return 1;			/* Success */
}
#endif	/* HAVE_INET_NTOP */

/* This is for Emacs's benefit:
 * Local Variables:	***
 * mode: C		***
 * fill-column:	75	***
 * End:			***
 */
