/* spc.c
 *
 * Functions for handling SPC (Serialized Procedure Call) protocol.
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: spc.c,v 2.10 2001-07-30 07:09:58 arensb Exp $
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>		/* For malloc() */

#if STDC_HEADERS
#  include <string.h>		/* For memcpy() */
#else	/* STDC_HEADERS */
# ifndef HAVE_STRCHR
#  define strchr index
#  define strrchr rindex
# endif /* HAVE_STRCHR */
# ifndef HAVE_MEMCPY
#  define memcpy(d,s,n)		bcopy ((s), (d), (n))
#  define memmove(d,s,n)	bcopy ((s), (d), (n))
# endif /* HAVE_MEMCPY */
#endif	/* STDC_HEADERS */

#if HAVE_SYS_PARAM_H
#  include <sys/param.h>		/* For ntohs() and friends */
#endif	/* HAVE_SYS_PARAM_H */
#if HAVE_NETINET_IN_H
#  include <netinet/in.h>		/* For ntohs() and friends, under
					 * Linux */
#endif	/* HAVE_NETINET_IN_H */

#include "pconn/pconn.h"	/* For DLP and debug_dump() */
#include "coldsync.h"
#include "spc.h"
#include "cs_error.h"

static void pack_dlp_time(const struct dlp_time *t,
			  unsigned char **ptr);
static void pack_dbinfo(const struct dlp_dbinfo *dbinfo,
			unsigned char *buf);

/* spc_send
 * Takes an SPC header, the data associated with it, and a pointer to
 * someplace to return the results.
 * spc_send() parses the header and does the Right Thing with it,
 * depending on the opcode in the header. In the case of DLP requests,
 * this means passing the data along to the Palm and reading a
 * response.
 * spc_send() modifies 'header', filling in the return status for the
 * request, and changing its 'len' field to the length of the return
 * data.
 * NB: the data returned in '*outbuf', if any, is allocated by
 * spc_send(); it is the caller's responsibility to free it
 * afterwards.
 */
int
spc_send(struct spc_hdr *header,		/* SPC header */
	 PConnection *pconn,			/* Connection to Palm */
	 const struct dlp_dbinfo *dbinfo,	/* Current database */
	 const unsigned char *inbuf,		/* Request data */
	 unsigned char **outbuf)		/* Response data (allocated) */
{
	int err;

	SYNC_TRACE(7)
	{
		fprintf(stderr, "* Inside spc_send(%d, (%d), %ld))\n",
			header->op, header->status, header->len);
		debug_dump(stderr, "SPC", inbuf, header->len);
	}

	/* Decide what to do based on the opcode */
	switch (header->op)
	{
	    case SPCOP_NOP:		/* Do nothing */
		header->status = SPCERR_OK;
		header->len = 0L;
		*outbuf = NULL;		/* No return data */
		return 0;

	    case SPCOP_DBINFO:		/* Return information about database */
		if ((*outbuf = (unsigned char *)
		     malloc(DLPCMD_DBINFO_LEN + DLPCMD_DBNAME_LEN))
		    == NULL)
			return SPCERR_NOMEM;
		header->len = DLPCMD_DBINFO_LEN + DLPCMD_DBNAME_LEN;
		pack_dbinfo(dbinfo, *outbuf);
		header->status = SPCERR_OK;
		break;

	    case SPCOP_DLPC:		/* Send DLP command to Palm */
	    {
		    /* XXX - Really need to parse the command: a
		     * conduit can DlpOpenDB() a database, so if it
		     * dies unexpectedly without a corresponding
		     * DlpCloseDB(), this can cause a database handle
		     * leak.
		     */
		    const ubyte *padp_respbuf;
		    uword padp_resplen;

		    if (pconn == NULL)	/* Sanity check */
			    return -1;

		    err = padp_write(pconn, inbuf, header->len);
		    SYNC_TRACE(7)
			    fprintf(stderr,
				    "spc_send: padp_write returned %d\n",
				    err);
		    if (err < 0)	/* Problem with padp_write */
		    {
			    switch (palm_errno)
			    {
				case PALMERR_TIMEOUT:
				    cs_errno = CSE_NOCONN;
				    break;
				default:
				    break;
			    }
			    return -1;
		    }

		    err = padp_read(pconn, &padp_respbuf, &padp_resplen);
		    SYNC_TRACE(7)
		    {
			    fprintf(stderr,
				    "spc_send: padp_read returned %d\n",
				    err);
			    fprintf(stderr,
				    "spc_send: padp_resplen == %d\n",
				    padp_resplen);
		    }
		    if (err < 0)	/* Problem with padp_read */
		    {
			    if (palm_errno == PALMERR_EOF)
				    /* EOF. In practical terms, this
				     * means the connection to the
				     * Palm was lost.
				     */
				    cs_errno = CSE_NOCONN;
			    return -1;
		    }

		    *outbuf = malloc(padp_resplen);
		    if (*outbuf == NULL)	/* Out of memory */
			    return -1;

		    memcpy(*outbuf, padp_respbuf, padp_resplen);
		    header->status = SPCERR_OK;
		    header->len = padp_resplen;
		    return 0;		/* Success */
	    }
		break;

	    default:			/* Bad opcode */
		header->status = SPCERR_BADOP;
		header->len = 0L;
		*outbuf = NULL;		/* No return data */
		return 0;	/* Return success, because spc_send()
				 * successfully processed a malformed
				 * request.
				 */
	}

	return 0;		/* Success */
}

/* pack_dlp_time
 * Helper function. Writes 't' to the buffer pointed to by 'ptr', and
 * updates 'ptr' to point to the byte just after that.
 */
static void
pack_dlp_time(const struct dlp_time *t,
	      unsigned char **ptr)
{
	* ((unsigned short *) (*ptr)) = htons(t->year);
	*ptr += 2;
	**ptr = t->month;
	(*ptr)++;
	**ptr = t->day;
	(*ptr)++;
	**ptr = t->hour;
	(*ptr)++;
	**ptr = t->minute;
	(*ptr)++;
	**ptr = t->second;
	(*ptr)++;
	**ptr = 0;
	(*ptr)++;
}

/* pack_dbinfo
 * A private helper function: write 'dbinfo' to the buffer 'buf', with
 * proper padding and network byte ordering.
 */
static void
pack_dbinfo(const struct dlp_dbinfo *dbinfo,
	    unsigned char *buf)
{
	*buf++ = dbinfo->size;
	*buf++ = dbinfo->misc_flags;
	* ((unsigned short *) buf) = htons(dbinfo->db_flags);
	buf += 2;
	* ((unsigned long *) buf) = htonl(dbinfo->type);
	buf += 4;
	* ((unsigned long *) buf) = htonl(dbinfo->creator);
	buf += 4;
	* ((unsigned short *) buf) = htons(dbinfo->version);
	buf += 2;
	* ((unsigned long *) buf) = htonl(dbinfo->modnum);
	buf += 4;
	pack_dlp_time(&dbinfo->ctime, &buf);
	pack_dlp_time(&dbinfo->mtime, &buf);
	pack_dlp_time(&dbinfo->baktime, &buf);
	* ((unsigned short *) buf) = htons(dbinfo->index);
	buf += 2;
	memcpy(buf, dbinfo->name, DLPCMD_DBNAME_LEN);
}
