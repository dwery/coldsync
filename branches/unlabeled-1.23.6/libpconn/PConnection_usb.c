/* PConnection_usb.c
 *
 * USB-related stuff for Visors.
 *
 *	Copyright (C) 2000, Louis A. Mamakos.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: PConnection_usb.c,v 1.23.6.1 2001-07-28 18:05:52 arensb Exp $
 */

#include "config.h"

#include <stdio.h>	/* This is left outside of the #if WITH_USB solely
			 * to make 'gcc' shut up: apparently, ANSI C
			 * doesn't like empty source files.
			 */

#if WITH_USB		/* This encompasses the entire file */
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>

#if STDC_HEADERS
# include <string.h>		/* For memcpy() et al. */
#else	/* STDC_HEADERS */
# ifndef HAVE_STRCHR
#  define strchr index
#  define strrchr rindex
# endif	/* HAVE_STRCHR */
# ifndef HAVE_MEMCPY
#  define memcpy(d,s,n)		bcopy ((s), (d), (n))
#  define memmove(d,s,n)	bcopy ((s), (d), (n))
# endif	/* HAVE_MEMCPY */
#endif	/* STDC_HEADERS */

#include <dev/usb/usb.h>

#if HAVE_LIBINTL_H
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL_H */

#include "pconn/PConnection.h"
#include "pconn/palm_types.h"
#include "pconn/palm_errno.h"
#include "pconn/cmp.h"			/* XXX - This ought to go away */

#include "usb_generic.h"



static int
usb_bind(PConnection *pconn,
	 const void *addr,
	 const int addrlen)
{
	return slp_bind(pconn, (const struct slp_addr *) addr);
}

static int
usb_read(PConnection *p, unsigned char *buf, int len)
{

	return (USB_READ(p->fd, buf, len, p->io_private));
}

static int
usb_write(PConnection *p, unsigned const char *buf, const int len)
{

	return (USB_WRITE(p->fd, buf, len, p->io_private));
}

static int
usb_connect(PConnection *p, const void *addr, const int addrlen)
{
	return -1;		/* Not applicable to USB connection */
}

static int
usb_accept(PConnection *pconn)
{
	udword newspeed;		/* Not really a speed; this is just
					 * used for the return value from
					 * cmp_accept().
					 */

	/* Negotiate a CMP connection.
	 * Since this is USB, the speed is meaningless: it'll just go
	 * however fast it can.
	 */
	newspeed = cmp_accept(pconn, 0);
	if (newspeed == ~0)
		return -1;

	return 0;
}

static int
usb_close(PConnection *p)
{	

	/* Clean up the protocol stack elements */
	dlp_tini(p);
	padp_tini(p);
	slp_tini(p);

	USB_CLOSE(p->fd, p->io_private);
}

static int
usb_select(PConnection *p, pconn_direction which, struct timeval *tvp)
{

	return (USB_SELECT(p->fd, (which == forReading), tvp, p->io_private));
}

static int
usb_drain(PConnection *p)
{
	return 0;
}

int
pconn_usb_open(PConnection *pconn, char *device, int prompt)
{

	/* Initialize the various protocols that the serial connection will
	 * use.
	 */
	/* Initialize the SLP part of the PConnection */
	if (slp_init(pconn) < 0)
	{
		free(pconn);
		return -1;
	}

	/* Initialize the PADP part of the PConnection */
	if (padp_init(pconn) < 0)
	{
		padp_tini(pconn);
		slp_tini(pconn);
		return -1;
	}

	/* Initialize the DLP part of the PConnection */
	if (dlp_init(pconn) < 0)
	{
		dlp_tini(pconn);
		padp_tini(pconn);
		slp_tini(pconn);
		return -1;
	}

	/* Set the methods used by the USB connection */
	pconn->io_bind = &usb_bind;
	pconn->io_read = &usb_read;
	pconn->io_write = &usb_write;
	pconn->io_connect = &usb_connect;
	pconn->io_accept = &usb_accept;
	pconn->io_close = &usb_close;
	pconn->io_select = &usb_select;
	pconn->io_drain = &usb_drain;

	pconn->fd = USB_OPEN(device, prompt, &pconn->io_private);
	if (pconn->fd == -1) {
		dlp_tini(pconn);
		padp_tini(pconn);
		slp_tini(pconn);
		return -1;
	}

	return (pconn->fd);
}

#endif	/* WITH_USB */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
