/* dlp_cmd.c
 *
 * Functions for manipulating a remote Palm device via the Desktop Link
 * Protocol (DLP).
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * The functions in this file constitute the programmer-visible DLP
 * interface. They package their arguments up, pass them on to the DLP
 * protocol functions, interpret their results, and repackage them back for
 * return to the caller.
 *
 * $Id: dlp_cmd.c,v 1.28 2001-12-09 22:42:21 arensb Exp $
 */
/* XXX - When using serial-to-USB under Linux, a lot of these can hang.
 * It's possible to fix the netsync read/write functions to time out and
 * set an error code when Linux drops data, but in the current
 * implementation, it's not possible to restart the request.
 *
 * Add a dlpc_do_request() (or something) function that implements
 *	err = dlp_send_req(pconn, &header, argv);
 *	err = dlp_recv_resp(pconn, (ubyte) DLPCMD_WriteUserInfo,
 *			    &resp_header, &ret_argv);
 *
 * and use it in all the functions below. This function can watch for a
 * timeout while receiving the response, and resend the request.
 */
#include "config.h"
#include <stdio.h>

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

#include <stdlib.h>		/* For malloc() */

#if HAVE_LIBINTL_H
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL_H */

#include "pconn/dlp.h"
#include "pconn/dlp_cmd.h"
#include "pconn/util.h"
#include "pconn/palm_errno.h"

int dlpc_trace = 0;		/* Debugging level for DLP commands */

#define DLPC_TRACE(n)	if (dlpc_trace >= (n))

/* dlpcmd_gettime
 * Just a convenience function for reading a Palm date from a data buffer.
 */
static void
dlpcmd_gettime(const ubyte **rptr, struct dlp_time *t)
{
	t->year   = get_uword(rptr);
	t->month  = get_ubyte(rptr);
	t->day    = get_ubyte(rptr);
	t->hour   = get_ubyte(rptr);
	t->minute = get_ubyte(rptr);
	t->second = get_ubyte(rptr);
	get_ubyte(rptr);		/* Skip padding */
}

/* dlpcmd_puttime
 * Just a convenience function for writing a Palm date to a data buffer.
 */
static void
dlpcmd_puttime(ubyte **wptr, const struct dlp_time *t)
{
	put_uword(wptr, t->year);
	put_ubyte(wptr, t->month);
	put_ubyte(wptr, t->day);
	put_ubyte(wptr, t->hour);
	put_ubyte(wptr, t->minute);
	put_ubyte(wptr, t->second);
	put_ubyte(wptr, 0);		/* Padding */
}

/* DlpReadUserInfo
 * Read the user information from the Palm. The 'userinfo' struct will be
 * filled with this information.
 */
int
DlpReadUserInfo(PConnection *pconn,		/* Connection to Palm */
		struct dlp_userinfo *userinfo)
				/* Will be filled in with user information. */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	int max;		/* Prevents buffer overflows */

	DLPC_TRACE(1)
		fprintf(stderr, ">>> ReadUserInfo\n");

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_ReadUserInfo;
	header.argc = 0;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, NULL,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		rptr = ret_argv[i].data;
		switch (ret_argv[i].id)
		{
		    case DLPRET_ReadUserInfo_Info:
			/* XXX - Ideally, ought to make sure the size is
			 * sane: it should be >=
			 * DLPRETLEN_ReadUserInfo_Info, and should be large
			 * enough to hold everything that the return
			 * argument contains. Have to parse the argument a
			 * bit at a time to determine this, though, so it
			 * might not be worth the effort unless there's a
			 * buffer overflow exploit involved. I don't think
			 * there is, though there might be if this function
			 * is ever called in a case where 'userinfo' is a
			 * stack variable.
			 */
			userinfo->userid = get_udword(&rptr);
			userinfo->viewerid = get_udword(&rptr);
			userinfo->lastsyncPC = get_udword(&rptr);
			dlpcmd_gettime(&rptr, &(userinfo->lastgoodsync));
			dlpcmd_gettime(&rptr, &(userinfo->lastsync));
			userinfo->usernamelen = get_ubyte(&rptr);
			userinfo->passwdlen = get_ubyte(&rptr);

			max = sizeof(userinfo->username);
			if (userinfo->usernamelen < max)
				max = userinfo->usernamelen;
			memcpy(userinfo->username, rptr, max);
			rptr += userinfo->usernamelen;

			max = sizeof(userinfo->passwd);
			if (userinfo->passwdlen < max)
				max = userinfo->passwdlen;
			memcpy(userinfo->passwd, rptr, max);
			rptr += userinfo->passwdlen;

			DLPC_TRACE(1)
			{
				fprintf(stderr, "Got user info: user 0x%08lx, "
					"viewer 0x%08lx, last PC 0x%08lx\n",
					userinfo->userid,
					userinfo->viewerid,
					userinfo->lastsyncPC);
				fprintf(stderr,
					"Last successful sync %02d:%02d:%02d, "
					"%d/%d/%d\n",
					userinfo->lastgoodsync.hour,
					userinfo->lastgoodsync.minute,
					userinfo->lastgoodsync.second,
					userinfo->lastgoodsync.day,
					userinfo->lastgoodsync.month,
					userinfo->lastgoodsync.year);
				fprintf(stderr,
					"Last sync attempt %02d:%02d:%02d, "
					"%d/%d/%d\n",
					userinfo->lastsync.hour,
					userinfo->lastsync.minute,
					userinfo->lastsync.second,
					userinfo->lastsync.day,
					userinfo->lastsync.month,
					userinfo->lastsync.year);
				fprintf(stderr,
					"User name: (%d bytes) \"%*s\"\n",
					userinfo->usernamelen,
					userinfo->usernamelen-1,
					(userinfo->username == NULL ?
					 "(null)" : userinfo->username));
				fprintf(stderr, "DLPC: Password (%d bytes):\n",
					userinfo->passwdlen);
				debug_dump(stderr, "DLPC:",
					   userinfo->passwd,
					   userinfo->passwdlen);
			}
			break;

		    default:	/* Unknown argument type */
			fprintf(stderr,
				_("##### %s: Unknown argument type: "
				  "0x%02x.\n"),
				"DlpReadUserInfo",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

int
DlpWriteUserInfo(PConnection *pconn,	/* Connection to Palm */
		 const struct dlp_setuserinfo *userinfo)
				/* Bits of user information to set */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	static ubyte outbuf[DLPARGLEN_WriteUserInfo_UserInfo +
		DLPCMD_USERNAME_LEN];	/* Buffer holding outgoing arg */
	ubyte *wptr;		/* Pointer into buffers (for writing) */
	int max;		/* Prevents buffer overflows */

	DLPC_TRACE(1)
		fprintf(stderr, ">>> WriteUserInfo\n");
	DLPC_TRACE(3)
	{
		fprintf(stderr, "userinfo->userid == %ld\n",
			userinfo->userid);
		fprintf(stderr, "userinfo->viewerid == %ld\n",
			userinfo->viewerid);
		fprintf(stderr, "userinfo->lastsyncPC == 0x%08lx\n",
			userinfo->lastsyncPC);
		fprintf(stderr, "userinfo->modflags == 0x%02x\n",
			userinfo->modflags);
		fprintf(stderr, "userinfo->usernamelen == %d\n",
			userinfo->usernamelen);
		fprintf(stderr, "userinfo->username == \"%s\"\n",
			((userinfo->usernamelen == 0) ||
			 (userinfo->username == NULL) ?
			 "(null)" : userinfo->username));
	}

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_WriteUserInfo;
	header.argc = 1;

	/* Construct the argument buffer */
	wptr = outbuf;
	put_udword(&wptr, userinfo->userid);
	put_udword(&wptr, userinfo->viewerid);
	put_udword(&wptr, userinfo->lastsyncPC);
	dlpcmd_puttime(&wptr, &(userinfo->lastsync));
	put_ubyte(&wptr, userinfo->modflags);
	put_ubyte(&wptr, userinfo->usernamelen);
	if (userinfo->usernamelen > 0)
	{
		max = userinfo->usernamelen < DLPCMD_USERNAME_LEN ?
			userinfo->usernamelen :
			DLPCMD_USERNAME_LEN;

		memcpy(wptr, userinfo->username, max);
		wptr += max;
	}

	/* Fill in the argument */
	argv[0].id = DLPARG_WriteUserInfo_UserInfo;
	argv[0].size = wptr - outbuf;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr,
				_("##### %s: Unknown argument type: "
				  "0x%02x.\n"),
				"DlpWriteUserInfo",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

/* XXX - Ought to check version of DLP used by the Palm. If it's v1.2 or
 * later, send it an argument saying which version of DLP we understand.
 */
int
DlpReadSysInfo(PConnection *pconn,	/* Connection to Palm */
	       struct dlp_sysinfo *sysinfo)
				/* Will be filled in with system
				 * information */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */

	DLPC_TRACE(1)
		fprintf(stderr, ">>> ReadSysInfo\n");

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_ReadSysInfo;
	header.argc = 0;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, NULL,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Before parsing the arguments, set the DLP 1.2 fields to 0, for
	 * devices that don't return them.
	 */
	sysinfo->dlp_ver_maj = 0;
	sysinfo->dlp_ver_min = 0;
	sysinfo->comp_ver_maj = 0;
	sysinfo->comp_ver_min = 0;
	sysinfo->max_rec_size = 0L;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		rptr = ret_argv[i].data;
		switch (ret_argv[i].id)
		{
		    case DLPRET_ReadSysInfo_Info:
			sysinfo->rom_version = get_udword(&rptr);
			sysinfo->localization = get_udword(&rptr);
			get_ubyte(&rptr);		/* Skip padding */
			sysinfo->prodIDsize = get_ubyte(&rptr);
			sysinfo->prodID = get_udword(&rptr);

			DLPC_TRACE(1)
				fprintf(stderr,
					"Got sysinfo: ROM version 0x%08lx, "
					"loc. 0x%08lx, pIDsize %d, "
					"pID 0x%08lx\n",
					sysinfo->rom_version,
					sysinfo->localization,
					sysinfo->prodIDsize,
					sysinfo->prodID);
			break;

		    case DLPRET_ReadSysInfo_Ver:
		    {
			    sysinfo->dlp_ver_maj = get_uword(&rptr);
			    sysinfo->dlp_ver_min = get_uword(&rptr);
			    sysinfo->comp_ver_maj = get_uword(&rptr);
			    sysinfo->comp_ver_min = get_uword(&rptr);
			    sysinfo->max_rec_size = get_udword(&rptr);

			    DLPC_TRACE(1)
				    fprintf(stderr,
					    "Got version sysinfo: "
					    "DLP v%d.%d, "
					    "compatibility v%d.%d, "
					    "max record size 0x%08lx\n",
					    sysinfo->dlp_ver_maj,
					    sysinfo->dlp_ver_min,
					    sysinfo->comp_ver_maj,
					    sysinfo->comp_ver_min,
					    sysinfo->max_rec_size);
		    }
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr,
				_("##### %s: Unknown argument type: "
				  "0x%02x.\n"),
				"DlpReadSysInfo",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

int
DlpGetSysDateTime(PConnection *pconn,
		  struct dlp_time *ptime)
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */

	DLPC_TRACE(1)
		fprintf(stderr, ">>> GetSysDateTime\n");

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_GetSysDateTime;
	header.argc = 0;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, NULL,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		rptr = ret_argv[i].data;
		switch (ret_argv[i].id)
		{
		    case DLPRET_GetSysDateTime_Time:
			dlpcmd_gettime(&rptr, ptime);
			DLPC_TRACE(1)
				fprintf(stderr,
					"System time: %02d:%02d:%02d, "
					"%d/%d/%d\n",
					ptime->hour,
					ptime->minute,
					ptime->second,
					ptime->day,
					ptime->month,
					ptime->year);
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr,
				_("##### %s: Unknown argument type: "
				  "0x%02x.\n"),
				"DlpGetSysDateTime",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

int
DlpSetSysDateTime(PConnection *pconn,		/* Connection to Palm */
		  const struct dlp_time *ptime)	/* Time to set */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	static ubyte outbuf[DLPARGLEN_SetSysDateTime_Time];
					/* Buffer holding outgoing arg */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPC_TRACE(1)
		fprintf(stderr,
			">>> SetSysDateTime(%02d:%02d:%02d, %d/%d/%d)\n",
			ptime->hour,
			ptime->minute,
			ptime->second,
			ptime->day,
			ptime->month,
			ptime->year);

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_SetSysDateTime;
	header.argc = 1;

	/* Construct the argument buffer */
	wptr = outbuf;
	dlpcmd_puttime(&wptr, ptime);

	/* Fill in the argument */
	argv[0].id = DLPARG_SetSysDateTime_Time;
	argv[0].size = DLPARGLEN_SetSysDateTime_Time;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr,
				_("##### %s: Unknown argument type: "
				  "0x%02x.\n"),
				"DlpSetSysDateTime",
				ret_argv[i].id);
			continue;
		}
	}
	return 0;
}

int
DlpReadStorageInfo(PConnection *pconn,
		   const ubyte card,
		   ubyte *last_card,
		   ubyte *more,
		   struct dlp_cardinfo *cinfo)
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[2];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	static ubyte outbuf[DLPARGLEN_ReadStorageInfo_Req];
					/* Buffer holding outgoing arg */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */
	ubyte act_count = 0;	/* # card info structs returned */

	DLPC_TRACE(1)
		fprintf(stderr, ">>> ReadStorageInfo(%d)\n", card);

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_ReadStorageInfo;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, card);		/* Card number */
	put_ubyte(&wptr, 0);		/* Padding */

	/* Fill in the argument */
	argv[0].id = DLPARG_ReadStorageInfo_Req;
	argv[0].size = DLPARGLEN_ReadStorageInfo_Req;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Set the extended fields to zero, in case this Palm doesn't
	 * return them.
	 */
	cinfo->rom_dbs = 0;
	cinfo->ram_dbs = 0;
	cinfo->reserved1 = 0L;
	cinfo->reserved2 = 0L;
	cinfo->reserved3 = 0L;
	cinfo->reserved4 = 0L;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		rptr = ret_argv[i].data;
		switch (ret_argv[i].id)
		{
		    case DLPRET_ReadStorageInfo_Info:
			*last_card = get_ubyte(&rptr);
			*more = get_ubyte(&rptr);
			*more = 0;	/* XXX - more is set to 1 in all
					 * cases, but none of the
					 * subsequent calls to
					 * ReadStorageInfo return anything
					 * meaningful, either with xcopilot
					 * or on a real PalmPilot. Perhaps
					 * this really represents the
					 * number of memory cards?
					 */
			get_ubyte(&rptr);	/* Padding */
			act_count = get_ubyte(&rptr);

			/* XXX - There should be act_count of these. At
			 * some point, it'd be cool to add a loop.
			 */
			cinfo->totalsize = get_ubyte(&rptr);
			cinfo->cardno = get_ubyte(&rptr);
			cinfo->cardversion = get_uword(&rptr);
			dlpcmd_gettime(&rptr, &cinfo->ctime);
			cinfo->rom_size = get_udword(&rptr);
			cinfo->ram_size = get_udword(&rptr);
			cinfo->free_ram = get_udword(&rptr);
			cinfo->cardname_size = get_ubyte(&rptr);
			cinfo->manufname_size = get_ubyte(&rptr);
			memcpy(cinfo->cardname, rptr, cinfo->cardname_size);
			cinfo->cardname[cinfo->cardname_size] = '\0';
			rptr += cinfo->cardname_size;
			memcpy(cinfo->manufname, rptr, cinfo->manufname_size);
			cinfo->manufname[cinfo->manufname_size] = '\0';
			rptr += cinfo->manufname_size;

			/* Read a padding byte if we're not on an even-byte
			 * boundary (cinfo->totalsize is rounded up to an
			 * even number).
			 */
			if ((rptr - argv[i].data) & 1)
				get_ubyte(&rptr);

			break;
		    case DLPRET_ReadStorageInfo_Ext:
			cinfo->rom_dbs = get_uword(&rptr);
			cinfo->ram_dbs = get_uword(&rptr);
			cinfo->reserved1 = get_udword(&rptr);	/* Padding */
			cinfo->reserved2 = get_udword(&rptr);	/* Padding */
			cinfo->reserved3 = get_udword(&rptr);	/* Padding */
			cinfo->reserved4 = get_udword(&rptr);	/* Padding */
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr,
				_("##### %s: Unknown argument type: "
				  "0x%02x.\n"),
				"DlpReadStorageInfo",
				ret_argv[i].id);
			continue;
		}
	}


	DLPC_TRACE(6)
	{
		fprintf(stderr, "GetStorageInfo:\n");
		fprintf(stderr, "\tlastcard: %d\n", *last_card);
		fprintf(stderr, "\tmore: %d\n", *more);
		fprintf(stderr, "\tact_count: %d\n", act_count);
		fprintf(stderr, "\n");

		fprintf(stderr, "\ttotalsize == %d\n", cinfo->totalsize);
		fprintf(stderr, "\tcardno == %d\n", cinfo->cardno);
		fprintf(stderr, "\tcardversion == %d\n", cinfo->cardversion);
		fprintf(stderr, "\tctime == %02d:%02d:%02d, %d/%d/%d\n",
			cinfo->ctime.hour,
			cinfo->ctime.minute,
			cinfo->ctime.second,
			cinfo->ctime.day,
			cinfo->ctime.month,
			cinfo->ctime.year);
		fprintf(stderr, "\tROM: %ld, RAM: %ld, free RAM: %ld\n",
			cinfo->rom_size,
			cinfo->ram_size,
			cinfo->free_ram);
		fprintf(stderr, "\tcardname (%d): \"%*s\"\n",
			cinfo->cardname_size,
			cinfo->cardname_size,
			cinfo->cardname);
		fprintf(stderr, "\tmanufname (%d): \"%*s\"\n",
			cinfo->manufname_size,
			cinfo->manufname_size,
			cinfo->manufname);
		fprintf(stderr, "\n");
		fprintf(stderr, "\tROM dbs: %d\tRAM dbs: %d\n",
			cinfo->rom_dbs,
			cinfo->ram_dbs);
	}

	return 0;
}

int
DlpReadDBList(PConnection *pconn,	/* Connection to Palm */
	      const ubyte iflags,	/* Search flags */
	      const int card,		/* Card number */
	      const uword start,	/* Database index to start at */
	      uword *last_index,	/* Index of last entry returned */
	      ubyte *oflags,		/* Response flags */
	      ubyte *num,		/* # of dlp_dbinfo structs returned */
	      struct dlp_dbinfo *dbs)	/* Array of database info structs */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	static ubyte outbuf[DLPARGLEN_ReadDBList_Req];
					/* Output buffer */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */ 
	ubyte *wptr;		/* Pointer into buffers (for writing) */ 
	int max;		/* Prevents buffer overflows */

	DLPC_TRACE(1)
		fprintf(stderr,
			">>> ReadDBList flags 0x%02x, card %d, start %d\n",
			iflags, card, start);

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_ReadDBList;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, iflags);
	put_ubyte(&wptr, card);
	put_uword(&wptr, start);

	/* Fill in the argument */
	argv[0].id = DLPARG_ReadDBList_Req;
	argv[0].size = DLPARGLEN_ReadDBList_Req;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		rptr = ret_argv[i].data;
		switch (ret_argv[i].id)
		{
		    case DLPRET_ReadDBList_Info:
			*last_index = get_uword(&rptr);
			*oflags = get_ubyte(&rptr);
			*num = get_ubyte(&rptr);
			DLPC_TRACE(5)
				fprintf(stderr,
					"List header: last %d, flags 0x%02x,"
					" count %d\n",
					*last_index,
					*oflags,
					*num);

			/* Parse the info for this database */
			/* XXX - There might be multiple 'dlp_dbinfo'
			 * instances (unless the 'multiple' flag is set
			 * (DLPRET_READDBLFLAG_MORE)?). Cope with it.
			 */
			dbs->size = get_ubyte(&rptr);
			dbs->misc_flags = get_ubyte(&rptr);
			dbs->db_flags = get_uword(&rptr);
			dbs->type = get_udword(&rptr);
			dbs->creator = get_udword(&rptr);
			dbs->version = get_uword(&rptr);
			dbs->modnum = get_udword(&rptr);
			dlpcmd_gettime(&rptr, &dbs->ctime);
			dlpcmd_gettime(&rptr, &dbs->mtime);
			dlpcmd_gettime(&rptr, &dbs->baktime);
			dbs->index = get_uword(&rptr);

			/* XXX - Actually, should probably only read
			 * DLPCMD_DBNAME_LEN-1 (31) characters, and ensure
			 * a trailing NUL.
			 */
			/* Get the database name. Everything up to the end
			 * of the returned argument, or DLPCMD_DBNAME_LEN
			 * characters, whichever is shorter.
			 */
			max = ret_argv[i].data + ret_argv[i].size - rptr;
			if (max > DLPCMD_DBNAME_LEN)
				max = DLPCMD_DBNAME_LEN;
			memcpy(dbs->name, rptr, max);
			rptr += max;


			DLPC_TRACE(5)
			{
				fprintf(stderr, "Database info:\n");
				fprintf(stderr, "\tsize %d, misc flags 0x%02x,"
					" DB flags 0x%04x\n",
					dbs->size,
					dbs->misc_flags,
					dbs->db_flags);
				fprintf(stderr, "\tDB flags:");
				if (dbs->db_flags & DLPCMD_DBFLAG_RESDB)
					fprintf(stderr, " RESDB");
				if (dbs->db_flags & DLPCMD_DBFLAG_RO)
					fprintf(stderr, " RO");
				if (dbs->db_flags & DLPCMD_DBFLAG_APPDIRTY)
					fprintf(stderr, " APPDIRTY");
				if (dbs->db_flags & DLPCMD_DBFLAG_BACKUP)
					fprintf(stderr, " BACKUP");
				if (dbs->db_flags & DLPCMD_DBFLAG_OKNEWER)
					fprintf(stderr, " OKNEWER");
				if (dbs->db_flags & DLPCMD_DBFLAG_RESET)
					fprintf(stderr, " RESET");
				if (dbs->db_flags & DLPCMD_DBFLAG_OPEN)
					fprintf(stderr, " OPEN");
				fprintf(stderr, "\n");
				fprintf(stderr, "\ttype '%c%c%c%c' (0x%08lx), "
					"creator '%c%c%c%c' (0x%08lx), "
					"version %d, modnum %ld\n",
					(char) (dbs->type >> 24) & 0xff,
					(char) (dbs->type >> 16) & 0xff,
					(char) (dbs->type >> 8) & 0xff,
					(char) dbs->type & 0xff,
					dbs->type,
					(char) (dbs->creator >> 24) & 0xff,
					(char) (dbs->creator >> 16) & 0xff,
					(char) (dbs->creator >> 8) & 0xff,
					(char) dbs->creator & 0xff,
					dbs->creator,
					dbs->version,
					dbs->modnum);
				fprintf(stderr, "\tCreated %02d:%02d:%02d, "
					"%d/%d/%d\n",
					dbs->ctime.hour,
					dbs->ctime.minute,
					dbs->ctime.second,
					dbs->ctime.day,
					dbs->ctime.month,
					dbs->ctime.year);
				fprintf(stderr, "\tModified %02d:%02d:%02d, "
					"%d/%d/%d\n",
					dbs->mtime.hour,
					dbs->mtime.minute,
					dbs->mtime.second,
					dbs->mtime.day,
					dbs->mtime.month,
					dbs->mtime.year);
				fprintf(stderr, "\tBacked up %02d:%02d:%02d, "
					"%d/%d/%d\n",
					dbs->baktime.hour,
					dbs->baktime.minute,
					dbs->baktime.second,
					dbs->baktime.day,
					dbs->baktime.month,
					dbs->baktime.year);
				fprintf(stderr, "\tindex %d\n",
					dbs->index);
				fprintf(stderr, "\tName: \"%s\"\n",
					dbs->name);
			}
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr,
				_("##### %s: Unknown argument type: "
				  "0x%02x.\n"),
				"DlpReadDBList",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;
}

/* DlpOpenDB
 * Open a database.
 */
int
DlpOpenDB(PConnection *pconn,	/* Connection to Palm */
	  int card,		/* Memory card */
	  const char *name,	/* Database name */
	  ubyte mode,		/* Open mode */
	  ubyte *handle)	/* Handle to open database will be put here */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	static ubyte outbuf[DLPARGLEN_OpenDB_DB + DLPCMD_DBNAME_LEN];
					/* Buffer holding outgoing arg */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */
	int max;		/* To prevent buffer overruns */

	DLPC_TRACE(1)
		fprintf(stderr,
			">>> OpenDB: card %d, name \"%s\", mode 0x%02x\n",
			card, name, mode);

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_OpenDB;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, card);
	put_ubyte(&wptr, mode);

	/* XXX - How many significant characters (i.e., not counting the
	 * trailing NUL) are actually allowed? Need to check, here and in
	 * all functions that use ubyte outbuf[foo + DLPCMD_DBNAME_LEN];.
	 * Everywhere DLPCMD_DBNAME_LEN is used, for that matter.
	 */
	max = strlen(name);
	if (max > DLPCMD_DBNAME_LEN-1)
		max = DLPCMD_DBNAME_LEN-1;
	memcpy(wptr, name, max);
	wptr += max;
	put_ubyte(&wptr, 0);		/* Trailing NUL */

	/* Fill in the argument */
	argv[0].id = DLPARG_OpenDB_DB;
	argv[0].size = wptr - outbuf;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		rptr = ret_argv[i].data;
		switch (ret_argv[i].id)
		{
		    case DLPRET_OpenDB_DB:
			*handle = get_ubyte(&rptr);

			DLPC_TRACE(3)
				fprintf(stderr,
					"Database handle: %d\n",
					*handle);
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr,
				_("##### %s: Unknown argument type: "
				  "0x%02x.\n"),
				"DlpOpenDB",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

/* DlpCreateDB
 * Create a new database according to the specifications given in
 * 'newdb'.
 * Puts a handle for the newly-created database in 'handle'.
 */
int
DlpCreateDB(PConnection *pconn,		/* Connection to Palm */
	    const struct dlp_createdbreq *newdb,
				/* Describes the database to create */
	    ubyte *handle)	/* Database handle will be put here */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	static ubyte outbuf[DLPARGLEN_CreateDB_DB + DLPCMD_DBNAME_LEN];
					/* Buffer holding outgoing arg */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */
	int max;		/* To prevent buffer overflow */

	DLPC_TRACE(1)
		fprintf(stderr,
			">>> CreateDB: creator 0x%08lx, type 0x%08lx, "
			"card %d, flags 0x%02x, version %d, name \"%s\"\n",
			newdb->creator,
			newdb->type,
			newdb->card,
			newdb->flags,
			newdb->version,
			newdb->name);

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_CreateDB;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_udword(&wptr, newdb->creator);
	put_udword(&wptr, newdb->type);
	put_ubyte(&wptr, newdb->card);
	put_ubyte(&wptr, 0);		/* Padding */
	put_uword(&wptr, newdb->flags);
	put_uword(&wptr, newdb->version);
	max = strlen(newdb->name);
	if (max > DLPCMD_DBNAME_LEN-1)
		max = DLPCMD_DBNAME_LEN-1;
	memcpy(wptr, newdb->name, max);
	wptr += max;
	put_ubyte(&wptr, 0);		/* Trailing NUL */

	/* Fill in the argument */
	argv[0].id = DLPARG_CreateDB_DB;
	argv[0].size = wptr - outbuf;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		rptr = ret_argv[i].data;
		switch (ret_argv[i].id)
		{
		    case DLPRET_CreateDB_DB:
			*handle = get_ubyte(&rptr);

			DLPC_TRACE(3)
				fprintf(stderr,
					"Database handle: %d\n", *handle);
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr,
				_("##### %s: Unknown argument type: "
				  "0x%02x.\n"),
				"DlpCreateDB",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

/* DlpCloseDB
 * Close the database with the handle 'handle'. If 'handle' is
 * DLPCMD_CLOSEALLDBS, then all open databases will be closed.
 */
/* XXX - Under PalmOS 3.0, you can close a specific database and update the
 * backup and/or modification times. Support this (add a 'ubyte flags'
 * argument).
 */
int
DlpCloseDB(PConnection *pconn,		/* Connection to Palm */
	   ubyte handle)		/* Handle of database to close */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */

	DLPC_TRACE(1)
		fprintf(stderr, ">>> CloseDB(%d)\n", handle);

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_CloseDB;
	header.argc = 1;

	/* Decide which argument to send, depending on whether we're
	 * closing a single database, or all of them.
	 */
	if (handle == DLPCMD_CLOSEALLDBS)
	{
		/* We're closing all of the databases */
		/* Fill in the argument */
		argv[0].id = DLPARG_CloseDB_All;
		argv[0].size = DLPARGLEN_CloseDB_All;
		argv[0].data = NULL;
	} else {
		/* We're only closing one database */
		argv[0].id = DLPARG_CloseDB_One;
		argv[0].size = DLPARGLEN_CloseDB_One;
		argv[0].data = &handle;
	}

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr,
				_("##### %s: Unknown argument type: "
				  "0x%02x.\n"),
				"DlpCloseDB",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

/* DlpDeleteDB
 * Deletes a database. The database must be closed.
 */
int
DlpDeleteDB(PConnection *pconn,		/* Connection to Palm */
	    const int card,	/* Memory card */
	    const char *name)	/* Name of the database */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	static ubyte outbuf[DLPARGLEN_DeleteDB_DB + DLPCMD_DBNAME_LEN];
	ubyte *wptr;		/* Pointer into buffers (for writing) */
	int max;		/* To prevent buffer overruns */

	DLPC_TRACE(1)
		fprintf(stderr, ">>> DeleteDB: card %d, name \"%s\"\n",
			card, name);

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_DeleteDB;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, card);
	put_ubyte(&wptr, 0);		/* Padding */

	max = strlen(name);
	if (max > DLPCMD_DBNAME_LEN - 1)
		max = DLPCMD_DBNAME_LEN - 1;
	memcpy(wptr, name, max);
	wptr += max;
	put_ubyte(&wptr, 0);		/* Terminating NUL */

	/* Fill in the argument */
	argv[0].id = DLPARG_DeleteDB_DB;
	argv[0].size = wptr - outbuf;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr,
				_("##### %s: Unknown argument type: "
				  "0x%02x.\n"),
				"DlpDeleteDB",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

/* DlpReadAppBlock
 * Read the AppInfo block for a database.
 */
int
DlpReadAppBlock(PConnection *pconn,	/* Connection */
		const ubyte handle,	/* Database handle */
		const uword offset,	/* Where to start reading */
		const uword len,	/* Max # bytes to read */
		uword *size,		/* # bytes read returned here */
		const ubyte **data)	/* Set to the data read */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	static ubyte outbuf[DLPARGLEN_ReadAppBlock_Req];
					/* Output buffer */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPC_TRACE(1)
		fprintf(stderr, ">>> ReadAppBlock\n");

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_ReadAppBlock;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, handle);
	put_ubyte(&wptr, 0);		/* Padding */
	put_uword(&wptr, offset);
	put_uword(&wptr, len);

	/* Fill in the argument */
	argv[0].id = DLPARG_ReadAppBlock_Req;
	argv[0].size = DLPARGLEN_ReadAppBlock_Req;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		rptr = ret_argv[i].data;
		switch (ret_argv[i].id)
		{
		    case DLPRET_ReadAppBlock_Blk:
			*size = get_uword(&rptr);
			/* Return a pointer to the data. */
			*data = rptr;
			rptr += *size;

			DLPC_TRACE(3)
				fprintf(stderr,
					"block size: %d (0x%04x)\n",
					*size,
					*size);
			DLPC_TRACE(10)
				debug_dump(stderr, "APP: ", *data, *size);

			break;
		    default:	/* Unknown argument type */
			fprintf(stderr,
				_("##### %s: Unknown argument type: "
				  "0x%02x.\n"),
				"DlpReadAppBlock",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

/* DlpWriteAppBlock
 * Write the AppInfo block for a database.
 */
int
DlpWriteAppBlock(PConnection *pconn,	/* Connection */
		 const ubyte handle,	/* Database handle */
		 const uword len,	/* Length of data */
		 const ubyte *data)	/* The data to write */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	ubyte *outbuf = NULL;		/* Output buffer */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	if ((outbuf = (ubyte *) malloc(DLPARGLEN_WriteAppBlock_Block +
				       len)) == NULL)
	{
		fprintf(stderr, _("%s: Out of memory.\n"),
			"DlpWriteAppBlock");
		return -1;
	}

	DLPC_TRACE(1)
		fprintf(stderr, ">>> WriteAppBlock\n");

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_WriteAppBlock;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, handle);
	put_ubyte(&wptr, 0);		/* Unused */
	put_uword(&wptr, len);
	memcpy(wptr, data, len);
	wptr += len;

	/* Fill in the argument */
	argv[0].id = DLPARG_WriteAppBlock_Block;
	argv[0].size = DLPARGLEN_WriteAppBlock_Block + len;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	free(outbuf);			/* We're done with it now */
	outbuf = NULL;
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr, _("##### %s: Unknown argument type: "
					  "0x%02x\n"),
				"DlpWriteAppBlock",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

/* DlpReadSortBlock
 * Read the sort block for a database.
 * XXX - Not terribly well-tested.
 */
int
DlpReadSortBlock(PConnection *pconn,	/* Connection */
		 const ubyte handle,	/* Database handle */
		 const uword offset,	/* Where to start reading */
		 const uword len,	/* Max # bytes to read */
		 uword *size,		/* # bytes read returned here */
		 const ubyte **data)	/* Set to the data read */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	static ubyte outbuf[DLPARGLEN_ReadSortBlock_Req];
					/* Output buffer */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPC_TRACE(1)
		fprintf(stderr, ">>> ReadSortBlock\n");

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_ReadSortBlock;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, handle);
	put_ubyte(&wptr, 0);		/* Unused */
	put_uword(&wptr, offset);
	put_uword(&wptr, len);

	/* Fill in the argument */
	argv[0].id = DLPARG_ReadSortBlock_Req;
	argv[0].size = DLPARGLEN_ReadSortBlock_Req;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		rptr = ret_argv[i].data;
		switch (ret_argv[i].id)
		{
		    case DLPRET_ReadSortBlock_Blk:
			/* Return the data and its length to the caller */
			*size = ret_argv[i].size;
			/* Return a pointer to the data */
			*data = rptr;
			rptr += *size;
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr, _("##### %s: Unknown argument type: "
					  "0x%02x\n"),
				"DlpReadSortBlock",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

/* DlpWriteSortBlock
 * Write the database's sort block.
 * XXX - Not terribly well-tested.
 */
int
DlpWriteSortBlock(PConnection *pconn,	/* Connection to Palm */
		  const ubyte handle,	/* Database handle */
		  const uword len,	/* Length of data */
		  const ubyte *data)	/* The data to write */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	ubyte *outbuf = NULL;		/* Output buffer */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	if ((outbuf = (ubyte *) malloc(DLPARGLEN_WriteSortBlock_Block +
				       len)) == NULL)
	{
		fprintf(stderr, _("%s: Out of memory.\n"),
			"DlpWriteSortBlock");
		return -1;
	}

	DLPC_TRACE(1)
		fprintf(stderr, ">>> WriteSortBlock\n");

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_WriteSortBlock;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, handle);
	put_ubyte(&wptr, 0);		/* Unused */
	put_uword(&wptr, len);
	memcpy(wptr, data, len);
	wptr += len;

	/* Fill in the argument */
	argv[0].id = DLPARG_WriteSortBlock_Block;
	argv[0].size = DLPARGLEN_WriteSortBlock_Block + len;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	free(outbuf);			/* We're done with it now */
	outbuf = NULL;
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr, _("##### %s: Unknown argument type: "
					  "0x%02x\n"),
				"DlpWriteSortBlock",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

/* DlpReadNextModifiedRec
 * Read the next modified record in the open database whose handle is
 * 'handle'.
 * When there are no more modified records to be read, the DLP response
 * code will be DLPSTAT_NOTFOUND.
 */
int
DlpReadNextModifiedRec(
	PConnection *pconn,		/* Connection to Palm */
	const ubyte handle,		/* Database handle */
	struct dlp_recinfo *recinfo,	/* Record will be put here */
	const ubyte **data)		/* Record data returned here */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */

	DLPC_TRACE(1)
		fprintf(stderr, ">>> ReadNextModifiedRec: db %d\n", handle);

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_ReadNextModifiedRec;
	header.argc = 1;

	/* Fill in the argument */
	argv[0].id = DLPARG_ReadNextModifiedRec_Req;
	argv[0].size = DLPARGLEN_ReadNextModifiedRec_Req;
	argv[0].data = (void *) &handle;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		rptr = ret_argv[i].data;
		switch (ret_argv[i].id)
		{
		    case DLPRET_ReadNextModifiedRec_Rec:
			recinfo->id = get_udword(&rptr);
			recinfo->index = get_uword(&rptr);
			recinfo->size = get_uword(&rptr);
			recinfo->attributes = get_ubyte(&rptr);
			recinfo->category = get_ubyte(&rptr);
			*data = rptr;

			DLPC_TRACE(6)
			{
				fprintf(stderr, "Read a record (by ID):\n");
				fprintf(stderr, "\tID == 0x%08lx\n",
					recinfo->id);
				fprintf(stderr, "\tindex == 0x%04x\n",
					recinfo->index);
				fprintf(stderr, "\tsize == 0x%04x\n",
					recinfo->size);
				fprintf(stderr, "\tattributes == 0x%02x\n",
					recinfo->attributes);
				fprintf(stderr, "\tcategory == 0x%02x\n",
					recinfo->category);
			}
			DLPC_TRACE(10)
				debug_dump(stderr, "REC",
					   *data,
					   recinfo->size);
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr, _("##### %s: Unknown argument type: "
					  "0x%02x\n"),
				"DlpReadNextModifiedRec",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

int
DlpReadRecordByID(PConnection *pconn,	/* Connection to Palm */
		  const ubyte handle,	/* Database handle */
		  const udword id,	/* ID of record to read */
		  const uword offset,	/* Where to start reading */
		  const uword len,	/* Max # bytes to read */
		  struct dlp_recinfo *recinfo,
				/* Record info returned here */
		  const ubyte **data)
				/* Pointer to record data returned here */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	static ubyte outbuf[DLPARGLEN_ReadRecord_ByID];
					/* Output buffer */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPC_TRACE(1)
		fprintf(stderr,
			">>> ReadRecord ByID: handle %d, recid %ld, "
			"offset %d, len %d\n",
			handle, id, offset, len);

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_ReadRecord;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, handle);
	put_ubyte(&wptr, 0);		/* Padding */
	put_udword(&wptr, id);
	put_uword(&wptr, offset);
	put_uword(&wptr, len);

	/* Fill in the argument */
	argv[0].id = DLPARG_ReadRecord_ByID;
	argv[0].size = DLPARGLEN_ReadRecord_ByID;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		rptr = ret_argv[i].data;
		switch (ret_argv[i].id)
		{
		    case DLPRET_ReadRecord_Rec:
			recinfo->id = get_udword(&rptr);
			recinfo->index = get_uword(&rptr);
			recinfo->size = get_uword(&rptr);
			recinfo->attributes = get_ubyte(&rptr);
			recinfo->category = get_ubyte(&rptr);
			*data = rptr;

			DLPC_TRACE(6)
			{
				fprintf(stderr, "Read a record (by ID):\n");
				fprintf(stderr, "\tID == 0x%08lx\n",
					recinfo->id);
				fprintf(stderr, "\tindex == 0x%04x\n",
					recinfo->index);
				fprintf(stderr, "\tsize == 0x%04x\n",
					recinfo->size);
				fprintf(stderr, "\tattributes == 0x%02x\n",
					recinfo->attributes);
				fprintf(stderr, "\tcategory == 0x%02x\n",
					recinfo->category);
			}
			DLPC_TRACE(10)
			{
				fprintf(stderr, "\tdata:\n");
				debug_dump(stderr, "RR", *data, recinfo->size);
			}
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr,
				_("##### %s: Unknown argument type: "
				  "0x%02x.\n"),
				"DlpReadRecordByID",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

/* DlpReadRecordByIndex
 * Read the 'index'th record in the database whose handle is 'handle'.
 * Contrary to what one might believe, this only returns the response
 * header (not the data, as DlpReadRecordByID does). Presumably, PalmOS
 * really, really likes dealing with record IDs, and not indices. Hence,
 * you're supposed to use this function to get the ID of the record you
 * want to read, but call DlpReadRecordByID to actually read it.
 * Also, the returned recinfo->index value is the size of the record.
 * There might be some deep significance to this, but I don't know.
 */
int
DlpReadRecordByIndex(
	PConnection *pconn,		/* Connection to Palm */
	const ubyte handle,		/* Database handle */
	const uword index,		/* Record index */
	struct dlp_recinfo *recinfo)
				/* Record info will be put here */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	static ubyte outbuf[DLPARGLEN_ReadRecord_ByIndex];
					/* Output buffer */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPC_TRACE(1)
		fprintf(stderr,
			">>> ReadRecord ByIndex: handle %d, index %d\n",
			handle,
			index);

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_ReadRecord;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, handle);
	put_ubyte(&wptr, 0);		/* Padding */
	put_udword(&wptr, index);
	put_uword(&wptr, 0);		/* offset - unused */
	put_uword(&wptr, 0);		/* len - unused */

	/* Fill in the argument */
	argv[0].id = DLPARG_ReadRecord_ByIndex;
	argv[0].size = DLPARGLEN_ReadRecord_ByIndex;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		rptr = ret_argv[i].data;
		switch (ret_argv[i].id)
		{
		    case DLPRET_ReadRecord_Rec:
			recinfo->id = get_udword(&rptr);
			recinfo->index = get_udword(&rptr);
			recinfo->size = get_udword(&rptr);
			recinfo->attributes = get_ubyte(&rptr);
			recinfo->category = get_ubyte(&rptr);

			DLPC_TRACE(6)
			{
				fprintf(stderr, "Read a record (by index):\n");
				fprintf(stderr, "\tID == 0x%08lx\n",
					recinfo->id);
				fprintf(stderr, "\tindex == 0x%04x\n",
					recinfo->index);
				fprintf(stderr, "\tsize == 0x%04x\n",
					recinfo->size);
				fprintf(stderr, "\tattributes == 0x%02x\n",
					recinfo->attributes);
				fprintf(stderr, "\tcategory == 0x%02x\n",
					recinfo->category);
			}
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr,
				_("##### %s: Unknown argument type: "
				  "0x%02x.\n"),
				"DlpReadRecordByIndex",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

/* DlpWriteRecord
 * Write a record.
 */
int
DlpWriteRecord(PConnection *pconn,	/* Connection to Palm */
	       const ubyte handle,	/* Database handle */
	       const ubyte flags,
	       const udword id,		/* Record ID */
	       const ubyte attributes,	/* Record attributes */
	       const ubyte category,	/* Record category */
	       const udword len,	/* Length of record data */
	       const ubyte *data,	/* Record data */
	       udword *recid)		/* Record's new ID returned here */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	ubyte *outbuf;			/* Output buffer */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	if ((outbuf = malloc(DLPARGLEN_WriteRecord_Rec + len)) == NULL)
	{
		fprintf(stderr,
			_("DlpWriteRecord: Can't allocate output buffer.\n"));
		return -1;
	}

	DLPC_TRACE(1)
		fprintf(stderr,
			">>> WriteRecord: handle %d, flags 0x%02x, "
			"recid 0x%08lx, attr 0x%02x, category %d, len %ld\n",
			handle,
			flags,
			id,
			attributes,
			category,
			len);
	DLPC_TRACE(10)
	{
		fprintf(stderr, "Raw record data (%ld == 0x%04lx bytes):\n",
			len, len);
		debug_dump(stderr, "WR", data, len);
	}

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_WriteRecord;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, handle);
	put_ubyte(&wptr, flags | 0x80);
		/* The Palm header says that the high bit (0x80) should
		 * always be set.
		 */
	put_udword(&wptr, id);
	put_ubyte(&wptr, attributes);
		/* XXX - Attributes: under early versions of the protocol,
		 * only the "secret" attribute is allowed. As of 1.1,
		 * "deleted", "archived" and "dirty" are also allowed.
		 * Check these and clear the forbidden bits.
		 */
	put_ubyte(&wptr, category);
	memcpy(wptr, data, len);
	wptr += len;

	/* Fill in the argument */
	argv[0].id = DLPARG_WriteRecord_Rec;
	argv[0].size = wptr - outbuf;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	free(outbuf);			/* We're done with it now */
	outbuf = NULL;
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		rptr = ret_argv[i].data;
		switch (ret_argv[i].id)
		{
		    case DLPRET_WriteRecord_Rec:
			*recid = get_udword(&rptr);
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr,
				_("##### %s: Unknown argument type: "
				  "0x%02x.\n"),
				"DlpWriteRecord",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

/* DlpDeleteRecord
 * Delete a record in an open database.
 * Warning: this will cause a fatal error on the Palm (well, PalmPilot at
 * least) if the database isn't open for reading.
 * XXX - This ought to be broken up into three functions: DeleteRecord,
 * DeleteAllRecords, and DeleteRecordsByCategory.
 */
int
DlpDeleteRecord(PConnection *pconn,	/* Connection to Palm */
		const ubyte handle,	/* Database handle */
		const ubyte flags,	/* Flags */
		const udword recid)	/* Unique record ID */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	static ubyte outbuf[DLPARGLEN_DeleteRecord_Rec];
						/* Output buffer */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPC_TRACE(1)
		fprintf(stderr,
			">>> DeleteRecord: handle %d, flags 0x%02x, "
			"recid 0x%08lx\n",
			handle, flags, recid);

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_DeleteRecord;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, handle);
	put_ubyte(&wptr, flags);
	put_udword(&wptr, recid);

	/* Fill in the argument */
	argv[0].id = DLPARG_DeleteRecord_Rec;
	argv[0].size = DLPARGLEN_DeleteRecord_Rec;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr,
				_("##### %s: Unknown argument type: "
				  "0x%02x.\n"),
				"DlpDeleteRecord",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

int
DlpReadResourceByIndex(
	PConnection *pconn,		/* Connection to Palm */
	const ubyte handle,		/* Database handle */
	const uword index,		/* Resource index */
	const uword offset,		/* Offset into resource */
	const uword len,		/* #bytes to read (~0 == to the
					 * end) */
	struct dlp_resource *value,	/* Resource info returned here */
	const ubyte **data)		/* Resource data returned here */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	static ubyte outbuf[DLPARGLEN_ReadResource_ByIndex];
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPC_TRACE(1)
		fprintf(stderr,
			">>> ReadResourceByIndex: handle %d, index %d, "
			"offset %d, len %d\n",
			handle, index, offset, len);

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_ReadResource;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, handle);
	put_ubyte(&wptr, 0);		/* Padding */
	put_uword(&wptr, index);
	put_uword(&wptr, offset);
	put_uword(&wptr, len);

	/* Fill in the argument */
	argv[0].id = DLPARG_ReadResource_ByIndex;
	argv[0].size = DLPARGLEN_ReadResource_ByIndex;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		rptr = ret_argv[i].data;
		switch (ret_argv[i].id)
		{
		    case DLPRET_ReadResource_Rsrc:
			value->type = get_udword(&rptr);
			value->id = get_uword(&rptr);
			value->index = get_uword(&rptr);
			value->size = get_uword(&rptr);
			*data = rptr;

			DLPC_TRACE(3)
				fprintf(stderr,
					"Resource: type '%c%c%c%c' (0x%08lx), "
					"id %d, index %d, size %d\n",
					(char) (value->type >> 24) & 0xff,
					(char) (value->type >> 16) & 0xff,
					(char) (value->type >> 8) & 0xff,
					(char) value->type & 0xff,
					value->type,
					value->id,
					value->index,
					value->size);
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr, _("##### %s: Unknown argument type: "
					  "0x%02x\n"),
				"DlpReadResourceByIndex",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

int
DlpReadResourceByType(
	PConnection *pconn,		/* Connection to Palm */
	const ubyte handle,		/* Database handle */
	const udword type,		/* Resource type */
	const uword id,			/* Resource ID */
	const uword offset,		/* Offset into resource */
	const uword len,		/* # bytes to read */
	struct dlp_resource *value,	/* Resource info returned here */
	ubyte *data)			/* Resource data returned here */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	static ubyte outbuf[DLPARGLEN_ReadResource_ByType];
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */
	int max;		/* To prevent buffer overruns */

	DLPC_TRACE(1)
		fprintf(stderr,
			">>> ReadResourceByType: handle %d, type %ld, id %d, "
			"offset %d, len %d\n",
			handle, type, id, offset, len);

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_ReadResource;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, handle);
	put_ubyte(&wptr, 0);		/* Padding */
	put_udword(&wptr, type);
	put_uword(&wptr, id);
	put_uword(&wptr, offset);
	put_uword(&wptr, len);

	/* Fill in the argument */
	argv[0].id = DLPARG_ReadResource_ByType;
	argv[0].size = DLPARGLEN_ReadResource_ByType;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		rptr = ret_argv[i].data;
		switch (ret_argv[i].id)
		{
		    case DLPRET_ReadResource_Rsrc:
			value->type = get_udword(&rptr);
			value->id = get_uword(&rptr);
			value->index = get_uword(&rptr);
			value->size = get_uword(&rptr);
			max = value->size;
			if (max > len)
				/* XXX - Error message */
				max = len;
			memcpy(data, rptr, max);
			rptr += max;

			DLPC_TRACE(3)
				fprintf(stderr,
					"Resource: type '%c%c%c%c' (0x%08lx), "
					"id %d, index %d, size %d\n",
					(char) (value->type >> 24) & 0xff,
					(char) (value->type >> 16) & 0xff,
					(char) (value->type >> 8) & 0xff,
					(char) value->type & 0xff,
					value->type,
					value->id,
					value->index,
					value->size);
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr,
				_("##### %s: Unknown argument type: "
				  "0x%02x.\n"),
				"DlpReadResourceByType",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

int
DlpWriteResource(PConnection *pconn,	/* Connection to Palm */
		 const ubyte handle,	/* Database handle */
		 const udword type,	/* Resource type */
		 const uword id,	/* Resource ID */
		 const uword size,	/* Size of resource */
		 const ubyte *data)	/* Resource data */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	ubyte *outbuf;			/* Output buffer */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPC_TRACE(1)
		fprintf(stderr,
			"WriteResource: type '%c%c%c%c' (0x%08lx), id %d, "
			"size %d\n",
			(char) (type >> 24) & 0xff,
			(char) (type >> 16) & 0xff,
			(char) (type >> 8) & 0xff,
			(char) type & 0xff,
			type,
			id, size);

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_WriteResource;
	header.argc = 1;

	/* Construct the argument */
	if ((outbuf = (ubyte *) malloc(DLPARGLEN_WriteResource_Rsrc+size))
	    == NULL)
	{
		fprintf(stderr, _("%s: Out of memory.\n"),
			"DlpWriteResource");
		return -1;
	}
	wptr = outbuf;
	put_ubyte(&wptr, handle);
	put_ubyte(&wptr, 0);		/* Padding */
	put_udword(&wptr, type);
	put_uword(&wptr, id);
	put_uword(&wptr, size);
	memcpy(wptr, data, size);
	wptr += size;

	/* Fill in the argument */
	argv[0].id = DLPARG_WriteResource_Rsrc;
	argv[0].size = wptr - outbuf;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	free(outbuf);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr,
				_("##### %s: Unknown argument type: "
				  "0x%02x.\n"),
				"DlpWriteResource",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

int
DlpDeleteResource(PConnection *pconn,	/* Connection to Palm */
		  const ubyte handle,	/* Database handle */
		  const ubyte flags,	/* Request flags */
		  const udword type,	/* Resource type */
		  const uword id)	/* Resource ID */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	static ubyte outbuf[DLPARGLEN_DeleteResource_Res];
					/* Output buffer */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPC_TRACE(1)
		fprintf(stderr,
			">>> DeleteResource: handle %d, flags 0x%02x, "
			"type '%c%c%c%c' (0x%08lx), id %d\n",
			handle, flags,
			(char) (type >> 24) & 0xff,
			(char) (type >> 16) & 0xff,
			(char) (type >> 8) & 0xff,
			(char) type & 0xff,
			type,
			id);

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_DeleteResource;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, handle);
	put_ubyte(&wptr, flags);
	put_udword(&wptr, type);
	put_uword(&wptr, id);

	/* Fill in the argument */
	argv[0].id = DLPARG_DeleteResource_Res;
	argv[0].size = DLPARGLEN_DeleteResource_Res;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr,
				_("##### %s: Unknown argument type: "
				  "0x%02x.\n"),
				"DlpDeleteResource",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

/* DlpCleanUpDatabase
 * Delete any records that were marked as archived or deleted in the record
 * database.
 */
int
DlpCleanUpDatabase(
	PConnection *pconn,		/* Connection to Palm */
	const ubyte handle)		/* Database handle */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */

	DLPC_TRACE(1)
		fprintf(stderr, ">>> CleanUpDatabase: handle %d\n", handle);

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_CleanUpDatabase;
	header.argc = 1;

	/* Fill in the argument */
	argv[0].id = DLPARG_CleanUpDatabase_DB;
	argv[0].size = DLPARGLEN_CleanUpDatabase_DB;
	argv[0].data = (void *) &handle;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr,
				_("##### %s: Unknown argument type: "
				  "0x%02x.\n"),
				"DlpCleanUpDatabase",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

/* DlpResetSyncFlags
 * Reset any dirty flags on the database.
 */
int
DlpResetSyncFlags(PConnection *pconn,		/* Connection to Palm */
		  const ubyte handle)		/* Database handle */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */

	DLPC_TRACE(1)
		fprintf(stderr, ">>> ResetSyncFlags: handle %d\n", handle);

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_ResetSyncFlags;
	header.argc = 1;

	/* Fill in the argument */
	argv[0].id = DLPARG_ResetSyncFlags_DB;
	argv[0].size = DLPARGLEN_ResetSyncFlags_DB;
	argv[0].data = (void *) &handle;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr, _("##### %s: Unknown argument type: "
					  "0x%02x\n"),
				"DlpResetSyncFlags",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

/* XXX - The API probably could use some work */
int
DlpCallApplication(
	PConnection *pconn,		/* Connection to Palm */
	const udword version,		/* OS version, used for determining
					 * how to phrase the DLP call. */
	const struct dlp_appcall *appcall,
					/* Which application to call */
	const udword paramsize,		/* Size of the parameter */
	const ubyte *param,		/* The parameter data */
	struct dlp_appresult *result)	/* Application result */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	ubyte *outbuf = NULL;		/* Output buffer */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	/* If the Palm is using DLP 1.x, then 'outbuf' is larger than it
	 * needs to be, strictly speaking. We're just allocating as large a
	 * buffer as we might need.
	 */
	if ((outbuf = (ubyte *) malloc(DLPARGLEN_CallApplication_V2 +
				       paramsize)) == NULL)
	{
		fprintf(stderr, _("%s: Out of memory.\n"),
			"DlpCallApplication");
		return -1;
	}

	DLPC_TRACE(1)
		fprintf(stderr,
			">>> CallApplication: ver 0x%08lx, creator '%c%c%c%c' "
			"(0x%08lx), action %d, type '%c%c%c%c' (0x%08lx), "
			"paramsize %ld\n",
			version,
			(char) (appcall->creator >> 24) & 0xff,
			(char) (appcall->creator >> 16) & 0xff,
			(char) (appcall->creator >> 8) & 0xff,
			(char) appcall->creator & 0xff,
			appcall->creator,
			appcall->action,
			(char) (appcall->type >> 24) & 0xff,
			(char) (appcall->type >> 16) & 0xff,
			(char) (appcall->type >> 8) & 0xff,
			(char) appcall->type & 0xff,
			appcall->type,
			paramsize);

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_CallApplication;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	if (version < 0x02000000)	/* XXX - need constant */
	{
		/* Use PalmOS 1.x call */
		put_udword(&wptr, appcall->creator);
		put_uword(&wptr, appcall->action);
		put_uword(&wptr, paramsize);
	} else {
		/* Use PalmOS 2.x call */
		put_udword(&wptr, appcall->creator);
		put_udword(&wptr, appcall->type);
		put_uword(&wptr, appcall->action);
		put_udword(&wptr, paramsize);
		put_udword(&wptr, 0L);		/* reserved1 */
		put_udword(&wptr, 0L);		/* reserved2 */
	}
	if (paramsize > 0)
		memcpy(wptr, param, paramsize);
	wptr += paramsize;

	/* Fill in the argument */
	if (version < 0x02000000)	/* XXX - need constant */
		argv[0].id = DLPARG_CallApplication_V1;
	else
		argv[0].id = DLPARG_CallApplication_V2;
	argv[0].size = wptr - outbuf;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	free(outbuf);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		rptr = ret_argv[i].data;
		switch (ret_argv[i].id)
		{
		    case DLPRET_CallApplication_V1:
			result->action = get_uword(&rptr);
			result->result = get_uword(&rptr);
			result->size = get_uword(&rptr);

			memcpy(result->data, rptr, result->size);

			break;
		    case DLPRET_CallApplication_V2:
			result->result = get_udword(&rptr);
			result->size = get_udword(&rptr);
			/* The reserved fields aren't useful, but they
			 * might conceivably be interesting.
			 */
			result->reserved1 = get_udword(&rptr);
			result->reserved2 = get_udword(&rptr);

			memcpy(result->data, rptr, result->size);

			break;
		    default:	/* Unknown argument type */
/* XXX - Do this everywhere: */
/*  		palm_errno = PALMERR_BADRESID; */
			fprintf(stderr, _("##### %s: Unknown argument type: "
					  "0x%02x\n"),
				"DlpCallApplication",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

/* DlpResetSystem
 * Indicate that the Palm needs to be reset at the end of the sync.
 */
int
DlpResetSystem(PConnection *pconn)		/* Connection to Palm */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	const struct dlp_arg *ret_argv;		/* Response argument list */

	DLPC_TRACE(1)
		fprintf(stderr, ">>> ResetSystem\n");

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_ResetSystem;
	header.argc = 0;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, NULL,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr, _("##### %s: Unknown argument type: "
					  "0x%02x\n"),
				"DlpResetSystem",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

/* DlpAddSyncLogEntry
 *
 * Adds 'msg' to the sync log on the Palm. If 'msg' is longer than
 * DLPC_MAXLOGLEN characters in length (not counting the final NUL), then
 * only the last DLPC_MAXLOGLEN characters are sent.
 */
int
DlpAddSyncLogEntry(PConnection *pconn,		/* Connection to Palm */
		   const char *msg)		/* Log message */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	int msglen;			/* Log message length */

	DLPC_TRACE(1)
		fprintf(stderr, ">>> AddSyncLogEntry \"%s\"\n", msg);

	/* Figure out the length of the message */
	msglen = strlen(msg);
	if (msglen <= 0)
		return 0;		/* Don't bother with 0-length logs */
	if (msglen > DLPC_MAXLOGLEN-1)
	{
		/* If 'msg' is too long, keep only the last
		 * DLPC_MAXLOGLEN-1 characters, since that's more likely to
		 * contain any errors that may have occurred at the end.
		 */
		msg += (msglen - DLPC_MAXLOGLEN + 1);
		msglen = DLPC_MAXLOGLEN-1;
	}

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_AddSyncLogEntry;
	header.argc = 1;

	/* Fill in the argument */
	argv[0].id = DLPARG_AddSyncLogEntry_Msg;
	argv[0].size = msglen + 1;
	argv[0].data = (ubyte *) msg;

	DLPC_TRACE(3)
		fprintf(stderr, "DlpAddSyncLogEntry: msg == [%.*s]\n",
			(int) argv[0].size, argv[0].data);

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr, _("##### %s: Unknown argument type: "
					  "0x%02x\n"),
				"DlpAddSyncLogEntry",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

/* DlplReadOpenDBInfo
 * Read information about an open database.
 */
int
DlpReadOpenDBInfo(PConnection *pconn,		/* Connection */
		  ubyte handle,			/* Database handle */
		  struct dlp_opendbinfo *dbinfo)
						/* Info about database */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */

	DLPC_TRACE(1)
		fprintf(stderr, ">>> ReadOpenDBInfo(%d)\n", handle);

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_ReadOpenDBInfo;
	header.argc = 1;

	/* Fill in the argument */
	argv[0].id = DLPARG_ReadOpenDBInfo_DB;
	argv[0].size = DLPARGLEN_ReadOpenDBInfo_DB;
	argv[0].data = &handle;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		rptr = ret_argv[i].data;
		switch (ret_argv[i].id)
		{
		    case DLPRET_ReadOpenDBInfo_Info:
			dbinfo->numrecs = get_uword(&rptr);
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr, _("##### %s: Unknown argument type: "
					  "0x%02x\n"),
				"DlpReadOpenDBInfo",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;
}

int
DlpMoveCategory(PConnection *pconn,	/* Connection to Palm */
		const ubyte handle,	/* Database handle */
		const ubyte from,	/* ID of source category */
		const ubyte to)		/* ID of destination category */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	static ubyte outbuf[DLPARGLEN_MoveCategory_Cat];
					/* Output buffer */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPC_TRACE(1)
		fprintf(stderr,
			">>> MoveCategory: handle %d, from %d, to %d\n",
			handle, from, to);

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_MoveCategory;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, handle);
	put_ubyte(&wptr, from);
	put_ubyte(&wptr, to);
	put_ubyte(&wptr, 0);		/* Padding */

	/* Fill in the argument */
	argv[0].id = DLPARG_MoveCategory_Cat;
	argv[0].size = DLPARGLEN_MoveCategory_Cat;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr, _("##### %s: Unknown argument type: "
					  "0x%02x\n"),
				"DlpMoveCategory",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

/* DlpOpenConduit
 * Sent before each conduit is opened by the desktop.
 */
int
DlpOpenConduit(PConnection *pconn)		/* Connection to Palm */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	const struct dlp_arg *ret_argv;		/* Response argument list */

	DLPC_TRACE(1)
		fprintf(stderr, ">>> OpenConduit:\n");

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_OpenConduit;
	header.argc = 0;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, NULL,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr, _("##### %s: Unknown argument type: "
					  "0x%02x\n"),
				"DlpOpenConduit",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

int
DlpEndOfSync(PConnection *pconn,	/* Connection to Palm */
	     const ubyte status)	/* Exit status, reason for
					 * termination */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	static ubyte status_buf[2];	/* Buffer for the status word */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPC_TRACE(1)
		fprintf(stderr, ">>> EndOfSync status %d\n", status);

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_EndOfSync;
	header.argc = 1;

	/* Construct the status buffer */
	wptr = status_buf;
	put_uword(&wptr, status);

	/* Fill in the argument */
	argv[0].id = DLPARG_EndOfSync_Status;
	argv[0].size = DLPARGLEN_EndOfSync_Status;
	argv[0].data = status_buf;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr, _("##### %s: Unknown argument type: "
					  "0x%02x\n"),
				"DlpEndOfSync",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

/* DlpResetRecordIndex
 * Reset the "next modified record" index to the beginning.
 */
int
DlpResetRecordIndex(
	PConnection *pconn,		/* Connection to Palm */
	const ubyte handle)		/* Database handle */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */

	DLPC_TRACE(1)
		fprintf(stderr, ">>> ResetRecordIndex: handle %d\n", handle);

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_ResetRecordIndex;
	header.argc = 1;

	/* Fill in the argument */
	argv[0].id = DLPARG_ResetRecordIndex_DB;
	argv[0].size = DLPARGLEN_ResetRecordIndex_DB;
	argv[0].data = (void *) &handle;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr, _("##### %s: Unknown argument type: "
					  "0x%02x\n"),
				"DlpResetRecordIndex",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

/* DlpReadRecordIDList
 * Read the list of record IDs in the database whose handle is
 * 'handle'.
 * NOTE: As of PalmOS 3.3, ReadRecordIDList apparently only returns up to
 * 500 record IDs at a time. Hence, when you call this function, you should
 * set it up in a loop to make sure that you've read all of the records:
 *
 *	uword numrecs;		// # of records in database
 *	uword ihave;		// # of record IDs read so far
 *	udword recids[FOO];	// Array of record IDs
 *
 *	ihave = 0;
 *	while (ihave < numrecs)
 *	{
 *		uword num_read;		// # record IDs read this time around
 *
 *		DlpReadRecordIDList(pconn, dbh, 0,
 *		                    ihave,
 *		                    numrecs - ihave,
 *		                    &num_read,
 *		                    recids + ihave);
 *		ihave += num_read;
 *	}
 */
int
DlpReadRecordIDList(
	PConnection *pconn,		/* Connection to Palm */
	const ubyte handle,		/* Database handle */
	const ubyte flags,
	const uword start,
	const uword max,		/* Max # entries to read */
	uword *numread,			/* How many entries were read */
	udword recids[])		/* IDs are returned here */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	static ubyte outbuf[DLPARGLEN_ReadRecordIDList_Req];
					/* Output buffer */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPC_TRACE(1)
		fprintf(stderr,
			">>> ReadRecordIDList: handle %d, flags 0x%02x, "
			"start %d, max %d\n",
			handle,
			flags,
			start,
			max);

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_ReadRecordIDList;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, handle);
	put_ubyte(&wptr, flags);
	put_uword(&wptr, start);
	put_uword(&wptr, max);

	/* Fill in the argument */
	argv[0].id = DLPARG_ReadRecordIDList_Req;
	argv[0].size = DLPARGLEN_ReadRecordIDList_Req;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		rptr = ret_argv[i].data;
		switch (ret_argv[i].id)
		{
		    case DLPRET_ReadRecordIDList_List:
			*numread = get_uword(&rptr);

			DLPC_TRACE(3)
				fprintf(stderr, "numread == %d\n", *numread);

			for (i = 0; i < *numread; i++)
			{
				if (i >= max)	/* Paranoia */
					break;
				recids[i] = get_udword(&rptr);
			}
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr, _("##### %s: Unknown argument type: "
					  "0x%02x\n"),
				"DlpReadRecordIDList",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

/* DlpReadNextRecInCategory
 * Read the next record in the given category. The record is returned in
 * 'record'.
 * XXX - Check to make sure the Palm understands v1.1 of the protocol.
 */
int
DlpReadNextRecInCategory(
	PConnection *pconn,		/* Connection to Palm */
	const ubyte handle,		/* Database handle */
	const ubyte category,		/* Category ID */
	struct dlp_recinfo *recinfo,	/* Record description returned here */
	const ubyte **data)		/* Record data returned here */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	static ubyte outbuf[DLPARGLEN_ReadNextRecInCategory_Rec];
					/* Output buffer */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPC_TRACE(1)
		fprintf(stderr,
			">>> ReadNextRecInCategory: handle %d, category %d\n",
			handle, category);

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_ReadNextRecInCategory;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, handle);
	put_ubyte(&wptr, category);

	/* Fill in the argument */
	argv[0].id = DLPARG_ReadNextRecInCategory_Rec;
	argv[0].size = DLPARGLEN_ReadNextRecInCategory_Rec;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		rptr = ret_argv[i].data;
		switch (ret_argv[i].id)
		{
		    case DLPRET_ReadNextRecInCategory_Rec:
			recinfo->id = get_udword(&rptr);
			recinfo->index = get_uword(&rptr);
			recinfo->size = get_uword(&rptr);
			recinfo->attributes = get_ubyte(&rptr);
			recinfo->category = get_ubyte(&rptr);
			*data = rptr;

			DLPC_TRACE(6)
			{
				fprintf(stderr,
					"Read record in category %d:\n",
					category);
				fprintf(stderr, "\tID == 0x%08lx\n",
					recinfo->id);
				fprintf(stderr, "\tindex == 0x%04x\n",
					recinfo->index);
				fprintf(stderr, "\tsize == 0x%04x\n",
					recinfo->size);
				fprintf(stderr, "\tattributes == 0x%02x\n",
					recinfo->attributes);
				fprintf(stderr, "\tcategory == 0x%02x\n",
					recinfo->category);
			}
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr, _("##### %s: Unknown argument type: "
					  "0x%02x\n"),
				"DlpReadNextRecInCategory",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

/* DlpReadNextModifiedRecInCategory
 * Read the next modified record in the given category. The record is
 * returned in 'record'.
 * XXX - Check to make sure the Palm understands v1.1 of the protocol.
 */
int
DlpReadNextModifiedRecInCategory(
	PConnection *pconn,		/* Connection to Palm */
	const ubyte handle,		/* Database handle */
	const ubyte category,		/* Category ID */
	struct dlp_recinfo *recinfo,	/* Record description returned here */
	const ubyte **data)		/* Record data returned here */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	static ubyte outbuf[DLPARGLEN_ReadNextModifiedRecInCategory_Rec];
					/* Output buffer */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPC_TRACE(1)
		fprintf(stderr,
			">>> ReadNextModifiedRecInCategory: handle %d, "
			"category %d\n",
			handle, category);

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_ReadNextModifiedRecInCategory;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, handle);
	put_ubyte(&wptr, category);

	/* Fill in the argument */
	argv[0].id = DLPARG_ReadNextModifiedRecInCategory_Rec;
	argv[0].size = DLPARGLEN_ReadNextModifiedRecInCategory_Rec;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		rptr = ret_argv[i].data;
		switch (ret_argv[i].id)
		{
		    case DLPRET_ReadNextModifiedRecInCategory_Rec:
			recinfo->id = get_udword(&rptr);
			recinfo->index = get_uword(&rptr);
			recinfo->size = get_uword(&rptr);
			recinfo->attributes = get_ubyte(&rptr);
			recinfo->category = get_ubyte(&rptr);
			*data = rptr;

			DLPC_TRACE(6)
			{
				fprintf(stderr,
					"Read record in category %d:\n",
					category);
				fprintf(stderr, "\tID == 0x%08lx\n",
					recinfo->id);
				fprintf(stderr, "\tindex == 0x%04x\n",
					recinfo->index);
				fprintf(stderr, "\tsize == 0x%04x\n",
					recinfo->size);
				fprintf(stderr, "\tattributes == 0x%02x\n",
					recinfo->attributes);
				fprintf(stderr, "\tcategory == 0x%02x\n",
					recinfo->category);
			}
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr, _("##### %s: Unknown argument type: "
					  "0x%02x\n"),
				"DlpReadNextModifiedRecInCategory",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

int
DlpReadAppPreference(
	PConnection *pconn,		/* Connection to Palm */
	const udword creator,		/* Application creator */
	const uword id,			/* Preference ID */
	const uword len,		/* Max # bytes to return */
	const ubyte flags,
	struct dlp_apppref *pref,
	ubyte *data)
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	static ubyte outbuf[DLPARGLEN_ReadAppPreference_Pref];
					/* Output buffer */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */
	int max;		/* Prevents buffer overflows */

	DLPC_TRACE(1)
		fprintf(stderr,
			">>> ReadAppPreference: creator '%c%c%c%c' (0x%08lx), "
			"id %d, len %d, flags 0x%02x\n",
			(char) (creator >> 24) & 0xff,
			(char) (creator >> 16) & 0xff,
			(char) (creator >> 8) & 0xff,
			(char) creator & 0xff,
			creator,
			id, len, flags);

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_ReadAppPreference;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_udword(&wptr, creator);
	put_uword(&wptr, id);
	put_uword(&wptr, len);
	put_ubyte(&wptr, flags);
	put_ubyte(&wptr, 0);		/* Padding */

	/* Fill in the argument */
	argv[0].id = DLPARG_ReadAppPreference_Pref;
	argv[0].size = DLPARGLEN_ReadAppPreference_Pref;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		rptr = ret_argv[i].data;
		switch (ret_argv[i].id)
		{
		    case DLPRET_ReadAppPreference_Pref:
			pref->version = get_uword(&rptr);
			pref->size = get_uword(&rptr);
			pref->len = get_uword(&rptr);

			max = len < pref->len ? len : pref->len;
			memcpy(data, rptr, max);
			rptr += pref->len;

			DLPC_TRACE(3)
				fprintf(stderr,
					"Read an app. preference: version %d, "
					"size %d, len %d\n",
					pref->version, pref->size, pref->len);
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr, _("##### %s: Unknown argument type: "
					  "0x%02x\n"),
				"DlpReadAppPreference",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

/* DlpWriteAppPreference
 * XXX - Test this.
 */
int
DlpWriteAppPreference(
	PConnection *pconn,		/* Connection to Palm */
	const udword creator,		/* Application creator */
	const uword id,			/* Preference ID */
	const ubyte flags,		/* Flags */
	const struct dlp_apppref *pref,	/* Preference descriptor */
	const ubyte *data)		/* Preference data */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	ubyte *outbuf = NULL;		/* Output buffer */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	if ((outbuf = (ubyte *) malloc(DLPARGLEN_WriteAppPreference_Pref +
				       pref->size)) == NULL)
	{
		fprintf(stderr, _("%s: Out of memory.\n"),
			"DlpWriteAppPreference");
		return -1;
	}

	DLPC_TRACE(1)
		fprintf(stderr, ">>> WriteAppPreference: XXX\n");

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_WriteAppPreference;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_udword(&wptr, creator);
	put_uword(&wptr, id);
	put_uword(&wptr, pref->version);
	put_uword(&wptr, pref->size);
	put_ubyte(&wptr, flags);
	put_ubyte(&wptr, 0);		/* Padding */
	memcpy(outbuf, data, pref->size);
	wptr += pref->size;

	/* Fill in the argument */
	argv[0].id = DLPARG_WriteAppPreference_Pref;
	argv[0].size = wptr - outbuf;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	free(outbuf);			/* We don't need this anymore */
	outbuf = NULL;
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr, _("##### %s: Unknown argument type: "
					  "0x%02x\n"),
				"DlpWriteAppPreference",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

/* DlpReadNetSyncInfo
 * Read the NetSync information about this Palm: the name and address of
 * the host it net-syncs with.
 * XXX - Check to make sure the Palm understands v1.1 of the protocol.
 */
int
DlpReadNetSyncInfo(PConnection *pconn,
		   struct dlp_netsyncinfo *netsyncinfo)
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	int max;		/* Prevents buffer overflows */

	DLPC_TRACE(1)
		fprintf(stderr, ">>> ReadNetSyncInfo\n");

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_ReadNetSyncInfo;
	header.argc = 0;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		rptr = ret_argv[i].data;
		switch (ret_argv[i].id)
		{
		    case DLPRET_ReadNetSyncInfo_Info:
			netsyncinfo->lansync_on = get_ubyte(&rptr);
			netsyncinfo->reserved1 = get_ubyte(&rptr);
			netsyncinfo->reserved1b = get_udword(&rptr);
			netsyncinfo->reserved2 = get_udword(&rptr);
			netsyncinfo->reserved3 = get_udword(&rptr);
			netsyncinfo->reserved4 = get_udword(&rptr);
			netsyncinfo->hostnamesize = get_uword(&rptr);
			netsyncinfo->hostaddrsize = get_uword(&rptr);
			netsyncinfo->hostnetmasksize = get_uword(&rptr);

			/* Fill in the address and hostname */
			max = sizeof(netsyncinfo->hostname);
			if (netsyncinfo->hostnamesize < max)
				max = netsyncinfo->hostnamesize;
			memcpy(netsyncinfo->hostname, rptr, max);
			rptr += netsyncinfo->hostnamesize;

			max = sizeof(netsyncinfo->hostaddr);
			if (netsyncinfo->hostaddrsize < max)
				max = netsyncinfo->hostaddrsize;
			memcpy(netsyncinfo->hostaddr, rptr, max);
			rptr += netsyncinfo->hostaddrsize;

			max = sizeof(netsyncinfo->hostnetmask);
			if (netsyncinfo->hostnetmasksize < max)
				max = netsyncinfo->hostnetmasksize;
			memcpy(netsyncinfo->hostnetmask, rptr, max);
			rptr += netsyncinfo->hostnetmasksize;

			DLPC_TRACE(6)
			{
				fprintf(stderr, "NetSync info:\n");
				fprintf(stderr, "\tLAN sync: %d\n",
					netsyncinfo->lansync_on);
				fprintf(stderr, "\thostname: (%d) \"%s\"\n",
					netsyncinfo->hostnamesize,
					netsyncinfo->hostname);
				fprintf(stderr, "\taddress: (%d) \"%s\"\n",
					netsyncinfo->hostaddrsize,
					netsyncinfo->hostaddr);
				fprintf(stderr, "\tnetmask: (%d) \"%s\"\n",
					netsyncinfo->hostnetmasksize,
					netsyncinfo->hostnetmask);
			}
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr, _("##### %s: Unknown argument type: "
					  "0x%02x\n"),
				"DlpReadNetSyncInfo",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

/* DlpWriteNetSyncInfo
 * Write new NetSync info to the Palm.
 * XXX - It might be good to add a special value (e.g., 0xffff) to the
 * netsyncinfo->*size fields to tell this function to use strlen() to
 * compute the sizes.
 * XXX - This works for the hostname, but not for binary strings.
 * XXX - Check to make sure the Palm understands v1.1 of the protocol.
 */
int
DlpWriteNetSyncInfo(PConnection *pconn,		/* Connection to Palm */
		    const ubyte modflags,	/* Which fields have
						 * changed? */
		    const struct dlp_netsyncinfo *newinfo)
						/* New values */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	static ubyte *outbuf = NULL;	/* Output buffer */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	/* XXX - This is so that ElectricFence can find overruns.
	 */
	if (outbuf == NULL)
		/* XXX - Figure out how big this needs to be */
		outbuf = malloc(2048);

	DLPC_TRACE(1)
		fprintf(stderr,
			">>> WriteNetSyncInfo: mod 0x%02x, LAN %d, name (%d) "
			"\"%s\", addr (%d) \"%s\", mask (%d) \"%s\"\n",
			modflags,
			newinfo->lansync_on,
			newinfo->hostnamesize,
			newinfo->hostname,
			newinfo->hostaddrsize,
			newinfo->hostaddr,
			newinfo->hostnetmasksize,
			newinfo->hostnetmask);

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_WriteNetSyncInfo;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, modflags);
	put_ubyte(&wptr, newinfo->lansync_on);
	put_udword(&wptr, 0);		/* reserved1b */
	put_udword(&wptr, 0);		/* reserved2 */
	put_udword(&wptr, 0);		/* reserved3 */
	put_udword(&wptr, 0);		/* reserved4 */
	/* XXX - Should these use strlen()? */
	/* XXX - Potential buffer overruns */
	put_uword(&wptr, newinfo->hostnamesize);
	put_uword(&wptr, newinfo->hostaddrsize);
	put_uword(&wptr, newinfo->hostnetmasksize);
	memcpy(wptr, newinfo->hostname,
	       newinfo->hostnamesize);
	wptr += newinfo->hostnamesize;
	memcpy(wptr, newinfo->hostaddr,
	       newinfo->hostaddrsize);
	wptr += newinfo->hostaddrsize;
	memcpy(wptr, newinfo->hostnetmask,
	       newinfo->hostnetmasksize);
	wptr += newinfo->hostnetmasksize;

	/* Fill in the argument */
	argv[0].id = DLPARG_WriteNetSyncInfo_Info;
	argv[0].size = wptr - outbuf;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr, _("##### %s: Unknown argument type: "
					  "0x%02x\n"),
				"DlpWriteNetSyncInfo",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;
}

/* DlpReadFeature
 * Read a feature from the Palm.
 * XXX - Check to make sure the Palm understands v1.1 of the protocol.
 */
int
DlpReadFeature(PConnection *pconn,	/* Connection to Palm */
	       const udword creator,	/* Feature creator */
	       const word featurenum,	/* Number of feature to read */
	       udword *value)		/* Value of feature returned here */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	static ubyte outbuf[DLPARGLEN_ReadFeature_Req];
					/* Output buffer */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPC_TRACE(1)
		fprintf(stderr,
			">>> ReadFeature: creator '%c%c%c%c' (0x%08lx), "
			"number %d\n",
			(char) (creator >> 24) & 0xff,
			(char) (creator >> 16) & 0xff,
			(char) (creator >> 8) & 0xff,
			(char) creator & 0xff,
			creator,
			featurenum);

	/* Fill in the header values */
	header.id = (ubyte) DLPCMD_ReadFeature;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_udword(&wptr, creator);
	put_uword(&wptr, featurenum);

	/* Fill in the argument */
	argv[0].id = DLPARG_ReadFeature_Req;
	argv[0].size = DLPARGLEN_ReadFeature_Req;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_dlpc_req(pconn,
			   &header, argv,
			   &resp_header, &ret_argv);
	if (err < 0)
		return err;
	if (resp_header.error != (ubyte) DLPSTAT_NOERR)
		return resp_header.error;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		rptr = ret_argv[i].data;
		switch (ret_argv[i].id)
		{
		    case DLPRET_ReadFeature_Feature:
			*value = get_udword(&rptr);

			DLPC_TRACE(3)
				fprintf(stderr,
					"Read feature: 0x%08lx (%ld)\n",
					*value, *value);
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr,
				_("##### %s: Unknown argument type: "
				  "0x%02x.\n"),
				"DlpReadFeature",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
