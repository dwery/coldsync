/* spc_client.c
 *
 * Functions to manipulate Palm connections via SPC.
 *
 *	Copyright (C) 2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * This file was created by Fred Gylys-Colwell
 *
 * $Id: spc_client.c,v 1.4 2002-03-09 05:43:19 arensb Exp $
 */
#include "config.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#if  HAVE_SYS_SELECT_H
#  include <sys/select.h>		/* To make select() work rationally
					 * under AIX */
#endif	/* HAVE_SYS_SELECT_H */

#if HAVE_STRINGS_H
#  include <strings.h>			/* For bzero() under AIX */
#endif	/* HAVE_STRINGS_H */

#if HAVE_LIBINTL_H
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL_H */

#if HAVE_SYS_PARAM_H
#  include <sys/param.h>		/* For ntohs() and friends */
#endif	/* HAVE_SYS_PARAM_H */
#if HAVE_NETINET_IN_H
#  include <netinet/in.h>		/* For ntohs() and friends, under
					 * Linux */
#endif	/* HAVE_NETINET_IN_H */

#include "pconn/spc_client.h"

static int
spc_client_bind(PConnection *pconn,
	    const void *addr,
	    const int addrlen)
{
	IO_TRACE(2)
		fprintf(stderr, "--- TRYING TO BIND A CLIENT! --\n");
	return -1;
}

static int
spc_client_read(PConnection *p, unsigned char *buf, int len)
{
	int len1, len2;
	for(len1 = len; len1 > 0; len1 -= len2 ) {
		len2 = read(p->fd, buf, len);
		buf += len2;
	}
	return len;
}

static int
spc_client_write(PConnection *p, unsigned const char *buf, int len)
{
	int len1, len2;
	for(len1 = len; len1 > 0; len1 -= len2 ) {
		len2 =  write(p->fd, buf, len);
		buf += len2;
	}
	return len;
}

static int
spc_client_accept(PConnection *pconn)
{
	IO_TRACE(2)
		fprintf(stderr, "--- TRYING TO ACCEPT FOR CLIENT! --\n");
	return -1;
}

static int
spc_client_connect(PConnection *p, const void *addr, const int addrlen)
{
	IO_TRACE(2)
		fprintf(stderr, "spc_client: trying to connect --\n");
	return -1;		/* Not applicable to serial connection */
}

static int
spc_client_drain(PConnection *p)
{
	return 0;
}

static int
spc_client_close(PConnection *p)
{
	/* XXX - Ought to check protocol */
	dlp_tini(p);
	padp_tini(p);
	slp_tini(p);
	return (p->fd >= 0 ? close(p->fd) : 0);
}

static int
spc_client_select(PConnection *p,
	      pconn_direction which,
	      struct timeval *tvp) {
	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(p->fd, &fds);
	return (which == forReading) ? select(p->fd+1, &fds, NULL, NULL, tvp)
				     : select(p->fd+1, NULL, &fds, NULL, tvp);
}

static int
spc_dlp_read(PConnection *pconn,	/* Connection to Palm */
	     const ubyte **buf,		/* Buffer to put the packet in */
	     uword *len)		/* Length of received message */
{
	unsigned char spc_header[SPC_HEADER_LEN];
	struct spc_hdr header;
	int err;
	
	err = (*pconn->io_read)(pconn, spc_header, SPC_HEADER_LEN);
	if (err < 0)
	{
		fprintf(stderr, _("%s: Error reading SPC respnse header "
			"from coldsync."), "spc_dlp_read");
		return err;
	}
	header.op = ntohs(*((unsigned short *) spc_header));
	header.status = ntohl(* ((unsigned long *) (spc_header+2)));
	header.len = ntohl(* ((unsigned long *) (spc_header+4)));
	if (header.status != SPCERR_OK)
	{
		fprintf(stderr, _("%s: Error reading SPC respnse "
				 "from coldsync: %d.\n"),
			"spc_dlp_read", header.status);
		
		return -1;
	}

	if (header.len > 0)
	{
		/* Allocate space for the payload */
		if (pconn->net.inbuf == NULL)
		{
			pconn->net.inbuf = (ubyte *) malloc(header.len);
			/* XXX - Error-checking */
		} else {
			pconn->net.inbuf = (ubyte *)
				realloc(pconn->net.inbuf, header.len);
			/* XXX - Error-checking */
		}
		
		err = (*pconn->io_read)(pconn, pconn->net.inbuf, header.len);
		if (err < 0)
		{
			fprintf(stderr, _("%s: Error reading SPC respnse data "
					"from coldsync."), "spc_dlp_read");
			return 0;
		}
	}
	*buf = pconn->net.inbuf;	/* Tell caller where to find the
					 * data */
	*len = header.len;	        /* And how much of it there was */

	return 1;			/* Success */
}

static int
spc_dlp_write(PConnection *pconn,
	      const ubyte *buf,
	      const uword len)		/* XXX - Is this enough? */
{
	unsigned char spc_header[SPC_HEADER_LEN];
	int err;
	
	*((unsigned short *) spc_header) = htons(SPCOP_DLPC);
	*((unsigned short *) (spc_header+2)) = htons(0);
	*((unsigned long *) (spc_header+4)) = htonl(len);

	err = (*pconn->io_write)(pconn, spc_header, SPC_HEADER_LEN);
	if (err != SPC_HEADER_LEN)
	{
		fprintf(stderr,
			_("%s: error sending SPC/DLP request header."),
			"spc_dlp_write");
				/* XXX - What now? Abort? */
		return -1;
	}
	
	err = (*pconn->io_write)(pconn, buf, len);
	if (err < 0)
	{
		fprintf(stderr, _("%s: Error sending SPC/DLPC "
				  "request data."),
			"spc_dlp_write");
		/* XXX - What now? Abort? */
	}
	return err;
}

/* new_spc_client
 * Opens a new connection on the give file descriptor.
 * Returns NULL on failure.
 */
PConnection *
new_spc_client(int fd)
{
	PConnection *pconn;		/* New connection */

	/* Allocate space for the new connection */
	if ((pconn = (PConnection *) malloc(sizeof(PConnection)))
	    == NULL)
	{
		/* XXX */
		fprintf(stderr, _("Can't allocate new connection.\n"));
		return NULL;
	}

	/* Initialize the common part, if only in case the constructor fails */
	pconn->fd = fd;
	pconn->io_bind = &spc_client_bind;
	pconn->io_read = &spc_client_read;
	pconn->io_write = &spc_client_write;
	pconn->io_accept = &spc_client_accept;
	pconn->io_connect = &spc_client_connect;
	pconn->io_close = &spc_client_close;
	pconn->io_select = &spc_client_select;
	pconn->io_drain = &spc_client_drain;
	pconn->io_private = 0;
	pconn->speed = -1;
	pconn->io_private = NULL;

	/* Initialize the DLP part of the PConnection */
	if (dlp_init(pconn) < 0)
	{
		dlp_tini(pconn);
		return NULL;
	}
	pconn->dlp.read = spc_dlp_read;
	pconn->dlp.write = spc_dlp_write;

	return pconn;
}

/* unpack_dlp_time
 * The oposite of spc's pack_dlp_time
 */
static void
unpack_dlp_time(struct dlp_time *t, unsigned char **ptr)
{
	t->year = ntohs(* ((unsigned short *) (*ptr)));
	*ptr += 2;
	t->month = **ptr;
	(*ptr)++;
	t->day = **ptr;
	(*ptr)++;
	t->hour = **ptr;
	(*ptr)++;
	t->minute = **ptr;
	(*ptr)++;
	t->second = **ptr;
	(*ptr)++;
	(*ptr)++;
}

/* unpack_dbinfo
 * This is the oposite of spc's pack_dbinfo.
 */
static void
unpack_dbinfo(struct dlp_dbinfo *dbinfo, unsigned char *buf)
{
	dbinfo->size =	*buf++;
	dbinfo->misc_flags = *buf++;
	dbinfo->db_flags = ntohs(* ((unsigned short *) buf));
	buf += 2;
	dbinfo->type = ntohl(* ((unsigned long *) buf));
	buf += 4;
	dbinfo->creator = ntohl(* ((unsigned long *) buf));
	buf += 4;
	dbinfo->version = ntohs(* ((unsigned short *) buf));
	buf += 2;
	dbinfo->modnum = ntohl(* ((unsigned long *) buf));
	buf += 4;
	unpack_dlp_time(&dbinfo->ctime, &buf);
	unpack_dlp_time(&dbinfo->mtime, &buf);
	unpack_dlp_time(&dbinfo->baktime, &buf);
	dbinfo->index = ntohs(* ((unsigned short *) buf));
	buf += 2;
	memcpy(dbinfo->name, buf, DLPCMD_DBNAME_LEN);
}

/* Get the database information  using SPC.
 * returns 0 on success.
*/
int
spc_get_dbinfo(PConnection *pconn, struct dlp_dbinfo *info)
{
	unsigned char spc_header[SPC_HEADER_LEN];
	unsigned char info_buf[DLPCMD_DBINFO_LEN + DLPCMD_DBNAME_LEN];
	struct spc_hdr header;
	int err;
	
	*((unsigned short *) spc_header) = htons(SPCOP_DBINFO);
	*((unsigned short *) (spc_header+2)) = htons(0);
	*((unsigned long *) (spc_header+4)) = htonl(0);
	
	err = (*pconn->io_write)(pconn, spc_header, SPC_HEADER_LEN);
	if (err != SPC_HEADER_LEN)
	{
		fprintf(stderr,
			_("%s: error sending SPC DBINFO request header."),
			"spc_dlp_write");
				/* XXX - What now? Abort? */
		return -1;
	}
	
	err = (*pconn->io_read)(pconn, spc_header, SPC_HEADER_LEN);
	if (err < 0)
	{
		fprintf(stderr, _("%s: Error reading SPC respnse header "
				  "from coldsync."), "spc_get_dbinfo");
		return err;
	}
	header.op = ntohs(*((unsigned short *) spc_header));
	header.status = ntohl(* ((unsigned long *) (spc_header+2)));
	header.len = ntohl(* ((unsigned long *) (spc_header+4)));
	if (header.status != SPCERR_OK)
	{
		fprintf(stderr, _("%s: Error reading SPC respnse "
				  "from coldsync: %d.\n"),
			"spc_get_dbinfo", header.status);
		
		return -1;
	}
	
	if (header.len != DLPCMD_DBINFO_LEN + DLPCMD_DBNAME_LEN) 
	{
		fprintf(stderr, _("%s: Error reading SPC data "
				  "from coldsync: %d.\n"),
			"spc_get_dbinfo", header.status);
		
		return -1;
	}
	err = (*pconn->io_read)(pconn, info_buf, header.len);
	if (err < 0)
	{
		fprintf(stderr, _("%s: Error reading SPC respnse data "
				  "from coldsync."), "spc_get_dbinfo");
		return -1;
	}
	unpack_dbinfo(info, info_buf);
	
	return 0;			/* Success */
}


/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * c-basic-offset: 8 ***
 * End: ***
 */
