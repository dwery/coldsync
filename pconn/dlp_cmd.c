/* dlp_cmd.c
 *
 * Functions for manipulating a remote Palm device via the Desktop Link
 * Protocol (DLP).
 *
 * The functions in this file constitute the programmer-visible DLP
 * interface. They package their arguments up, pass them on to the DLP
 * protocol functions, interpret their results, and repackage them back for
 * return to the caller.
 *
 * $Id: dlp_cmd.c,v 1.8 1999-03-28 09:58:25 arensb Exp $
 */
#include <stdio.h>
#include <string.h>		/* For memcpy() et al. */
#include <stdlib.h>		/* For malloc() */
#include "dlp.h"
#include "dlp_cmd.h"
#include "util.h"
#include "palm_errno.h"

#define DLPC_DEBUG	1
#ifdef DLPC_DEBUG
int dlpc_debug = 0;

#define DLPC_TRACE(level, format...)		\
	if (dlpc_debug >= (level))		\
		fprintf(stderr, "DLPC:" format)

#endif	/* DLPC_DEBUG */

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
DlpReadUserInfo(struct PConnection *pconn,	/* Connection to Palm */
		struct dlp_userinfo *userinfo)
				/* Will be filled in with user information. */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */

	DLPC_TRACE(1, ">>> ReadUserInfo\n");

	/* Fill in the header values */
	header.id = DLPCMD_ReadUserInfo;
	header.argc = 0;

	/* Send the DLP request */
	err = dlp_send_req(pconn, &header, NULL);
	if (err < 0)
		return err;	/* Error */

	DLPC_TRACE(10, "DlpReadUserInfo: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_ReadUserInfo,
			    &resp_header, &ret_argv);
	if (err < 0)
		return err;	/* Error */

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		rptr = ret_argv[i].data;
		switch (ret_argv[i].id)
		{
		    case DLPRET_ReadUserInfo_Info:
			/* XXX - Make sure the size is sane */
			userinfo->userid = get_udword(&rptr);
			userinfo->viewerid = get_udword(&rptr);
			userinfo->lastsyncPC = get_udword(&rptr);
			dlpcmd_gettime(&rptr, &(userinfo->lastgoodsync));
			dlpcmd_gettime(&rptr, &(userinfo->lastsync));
			userinfo->usernamelen = get_ubyte(&rptr);
			userinfo->passwdlen = get_ubyte(&rptr);
			/* XXX - Potential buffer overflow */
			memcpy(userinfo->username, rptr,
			       userinfo->usernamelen);
			rptr += userinfo->usernamelen;
			/* XXX - Potential buffer overflow */
			memcpy(userinfo->passwd, rptr, userinfo->passwdlen);
			rptr += userinfo->passwdlen;

			DLPC_TRACE(1, "Got user info: user 0x%08lx, "
				   "viewer 0x%08lx, last PC 0x%08lx\n",
				   userinfo->userid,
				   userinfo->viewerid,
				   userinfo->lastsyncPC);
			DLPC_TRACE(1, "Last successful sync %02d:%02d:%02d, "
				   "%d/%d/%d\n",
				   userinfo->lastgoodsync.hour,
				   userinfo->lastgoodsync.minute,
				   userinfo->lastgoodsync.second,
				   userinfo->lastgoodsync.day,
				   userinfo->lastgoodsync.month,
				   userinfo->lastgoodsync.year);
			DLPC_TRACE(1, "Last sync attempt %02d:%02d:%02d, "
				   "%d/%d/%d\n",
				   userinfo->lastsync.hour,
				   userinfo->lastsync.minute,
				   userinfo->lastsync.second,
				   userinfo->lastsync.day,
				   userinfo->lastsync.month,
				   userinfo->lastsync.year);
			DLPC_TRACE(1, "User name: (%d bytes) \"%*s\"\n",
				   userinfo->usernamelen,
				   userinfo->usernamelen-1,
				   userinfo->username);
#if DLPC_DEBUG
			if (dlpc_debug >= 1)
			{
				fprintf(stderr, "DLPC: Password (%d bytes):\n",
					userinfo->passwdlen);
				debug_dump(stderr, "DLPC:",
					   userinfo->passwd,
					   userinfo->passwdlen);
			}
#endif	/* DLPC_DEBUG */
			break;

		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

int
DlpWriteUserInfo(struct PConnection *pconn,	/* Connection to Palm */
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

	DLPC_TRACE(1, ">>> WriteUserInfo\n");

	/* Fill in the header values */
	header.id = DLPCMD_WriteUserInfo;
	header.argc = 1;

	/* Construct the argument buffer */
	wptr = outbuf;
	put_udword(&wptr, userinfo->userid);
	put_udword(&wptr, userinfo->viewerid);
	put_udword(&wptr, userinfo->lastsyncPC);
	dlpcmd_puttime(&wptr, &(userinfo->lastsync));
	put_ubyte(&wptr, userinfo->modflags);
	put_ubyte(&wptr, userinfo->usernamelen);
	/* XXX - Probably shouldn't do this unless modifying the user
	 * name.
	 */
	/* XXX - Potential buffer overflow */
	memcpy(wptr, userinfo->username, userinfo->usernamelen);
	wptr += userinfo->usernamelen;

	/* Fill in the argument */
	argv[0].id = DLPARG_WriteUserInfo_UserInfo;
	argv[0].size = wptr - outbuf;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	DLPC_TRACE(10, "DlpWriteUserInfo: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_WriteUserInfo, &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

/* XXX - Add v1.2 support: tell which version of DLP we're using */
int
DlpReadSysInfo(struct PConnection *pconn,	/* Connection to Palm */
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

	DLPC_TRACE(1, ">>> ReadSysInfo\n");

	/* Fill in the header values */
	header.id = DLPCMD_ReadSysInfo;
	header.argc = 0;

	/* Send the DLP request */
	err = dlp_send_req(pconn, &header, NULL);
	if (err < 0)
		return err;

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_ReadSysInfo, &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

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

			DLPC_TRACE(1, "Got sysinfo: ROM version 0x%08lx, "
				   "loc. 0x%08lx, pIDsize %d, pID 0x%08lx\n",
				   sysinfo->rom_version,
				   sysinfo->localization,
				   sysinfo->prodIDsize,
				   sysinfo->prodID);
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

int
DlpGetSysDateTime(struct PConnection *pconn,
		  struct dlp_time *ptime)
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */

	DLPC_TRACE(1, ">>> GetSysDateTime\n");

	/* Fill in the header values */
	header.id = DLPCMD_GetSysDateTime;
	header.argc = 0;

	/* Send the DLP request */
	err = dlp_send_req(pconn, &header, NULL);
	if (err < 0)
		return err;

	err = dlp_recv_resp(pconn, DLPCMD_GetSysDateTime,
			    &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		rptr = ret_argv[i].data;
		switch (ret_argv[i].id)
		{
		    case DLPRET_GetSysDateTime_Time:
			dlpcmd_gettime(&rptr, ptime);
			DLPC_TRACE(1, "System time: %02d:%02d:%02d, %d/%d/%d\n",
				   ptime->hour,
				   ptime->minute,
				   ptime->second,
				   ptime->day,
				   ptime->month,
				   ptime->year);
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

int
DlpSetSysDateTime(struct PConnection *pconn,	/* Connection to Palm */
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

	DLPC_TRACE(1, ">>> SetSysDateTime(%02d:%02d:%02d, %d/%d/%d)\n",
		   ptime->hour,
		   ptime->minute,
		   ptime->second,
		   ptime->day,
		   ptime->month,
		   ptime->year);

	/* Fill in the header values */
	header.id = DLPCMD_SetSysDateTime;
	header.argc = 1;

	/* Construct the argument buffer */
	wptr = outbuf;
	dlpcmd_puttime(&wptr, ptime);

	/* Fill in the argument */
	argv[0].id = DLPARG_SetSysDateTime_Time;
	argv[0].size = DLPARGLEN_SetSysDateTime_Time;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	DLPC_TRACE(10, "DlpSetSysDateTime: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_SetSysDateTime,
			    &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
				ret_argv[i].id);
			continue;
		}
	}
	return 0;
}

int
DlpReadStorageInfo(struct PConnection *pconn,
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
	static ubyte outbuf[2048];
				/* XXX - Fixed size: bad! */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */
	ubyte act_count;	/* # card info structs returned */

	DLPC_TRACE(1, ">>> ReadStorageInfo(%d)\n", card);

	/* Fill in the header values */
	header.id = DLPCMD_ReadStorageInfo;
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
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	DLPC_TRACE(10, "DlpReadStorageInfo: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_ReadStorageInfo,
			    &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

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
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
				ret_argv[i].id);
			continue;
		}
	}


	DLPC_TRACE(6, "GetStorageInfo:\n");
	DLPC_TRACE(6, "\tlastcard: %d\n", *last_card);
	DLPC_TRACE(6, "\tmore: %d\n", *more);
	DLPC_TRACE(6, "\tact_count: %d\n", act_count);
	DLPC_TRACE(6, "\n");

	DLPC_TRACE(6, "\ttotalsize == %d\n", cinfo->totalsize);
	DLPC_TRACE(6, "\tcardno == %d\n", cinfo->cardno);
	DLPC_TRACE(6, "\tcardversion == %d\n", cinfo->cardversion);
	DLPC_TRACE(6, "\tctime == %02d:%02d:%02d, %d/%d/%d\n",
		   cinfo->ctime.hour,
		   cinfo->ctime.minute,
		   cinfo->ctime.second,
		   cinfo->ctime.day,
		   cinfo->ctime.month,
		   cinfo->ctime.year);
	DLPC_TRACE(6, "\tROM: %ld, RAM: %ld, free RAM: %ld\n",
		   cinfo->rom_size,
		   cinfo->ram_size,
		   cinfo->free_ram);
	DLPC_TRACE(6, "\tcardname (%d): \"%*s\"\n",
		   cinfo->cardname_size,
		   cinfo->cardname_size,
		   cinfo->cardname);
	DLPC_TRACE(6, "\tmanufname (%d): \"%*s\"\n",
		   cinfo->manufname_size,
		   cinfo->manufname_size,
		   cinfo->manufname);
	DLPC_TRACE(6, "\n");
	DLPC_TRACE(6, "\tROM dbs: %d\tRAM dbs: %d\n",
		   cinfo->rom_dbs,
		   cinfo->ram_dbs);

	return 0;
}

int
DlpReadDBList(struct PConnection *pconn,	/* Connection to Palm */
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

	DLPC_TRACE(1, ">>> ReadDBList flags 0x%02x, card %d, start %d\n",
		   iflags, card, start);

	/* Fill in the header values */
	header.id = DLPCMD_ReadDBList;
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
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	DLPC_TRACE(10, "DlpReadDBList: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_ReadDBList, &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

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
			DLPC_TRACE(5, "List header: last %d, flags 0x%02x,"
				   " count %d\n",
				   *last_index,
				   *oflags,
				   *num);

			/* Parse the info for this database */
			/* XXX - There might be multiple 'dlp_dbinfo'
			 * instances (unless the 'multiple' flag is set?).
			 * Cope with it.
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
			memcpy(dbs->name, rptr,
			       dbs->size - DLPCMD_DBNAME_LEN);
			rptr += dbs->size - DLPCMD_DBNAME_LEN;

#ifdef DLPC_DEBUG
			if (dlpc_debug >= 5)
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
#endif	/* DLPC_DEBUG */
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
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
DlpOpenDB(struct PConnection *pconn,	/* Connection to Palm */
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
	static ubyte outbuf[128];	/* Output buffer */
					/* XXX - Fixed size: bad! */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPC_TRACE(1, ">>> OpenDB: card %d, name \"%s\", mode 0x%02x\n",
		   card, name, mode);

	/* Fill in the header values */
	header.id = DLPCMD_OpenDB;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, card);
	put_ubyte(&wptr, mode);
	/* XXX - Potential buffer overflow */
	memcpy(wptr, name, strlen(name)+1);
	wptr += strlen(name)+1;

	/* Fill in the argument */
	argv[0].id = DLPARG_OpenDB_DB;
	argv[0].size = wptr - outbuf;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	DLPC_TRACE(10, "DlpOpenDB: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_OpenDB, &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		rptr = ret_argv[i].data;
		switch (ret_argv[i].id)
		{
		    case DLPRET_OpenDB_DB:
			*handle = get_ubyte(&rptr);

			DLPC_TRACE(3, "Database handle: %d\n", *handle);
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
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
DlpCreateDB(struct PConnection *pconn,		/* Connection to Palm */
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
	static ubyte outbuf[1024];	/* Output buffer */
					/* Fixed size: bad! Find out the
					 * maximum size it can be. */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPC_TRACE(1, ">>> CreateDB: creator 0x%08lx, type 0x%08lx, card %d, flags 0x%02x, version %d, name \"%s\"\n",
		   newdb->creator,
		   newdb->type,
		   newdb->card,
		   newdb->flags,
		   newdb->version,
		   newdb->name);

	/* Fill in the header values */
	header.id = DLPCMD_CreateDB;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_udword(&wptr, newdb->creator);
	put_udword(&wptr, newdb->type);
	put_ubyte(&wptr, newdb->card);
	put_ubyte(&wptr, 0);		/* Padding */
	put_uword(&wptr, newdb->flags);
	put_uword(&wptr, newdb->version);
	memcpy(wptr, newdb->name, strlen(newdb->name));
	wptr += strlen(newdb->name);
	put_ubyte(&wptr, 0);		/* Trailing NUL */

	/* Fill in the argument */
	argv[0].id = DLPARG_CreateDB_DB;
	argv[0].size = wptr - outbuf;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	DLPC_TRACE(10, "DlpCreateDB: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_CreateDB, &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		rptr = ret_argv[i].data;
		switch (ret_argv[i].id)
		{
		    case DLPRET_CreateDB_DB:
			*handle = get_ubyte(&rptr);

			DLPC_TRACE(3, "Database handle: %d\n", *handle);
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
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
int
DlpCloseDB(struct PConnection *pconn,		/* Connection to Palm */
	   ubyte handle)	/* Handle of database to delete */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */

	DLPC_TRACE(1, ">>> CloseDB(%d)\n", handle);

	/* Fill in the header values */
	header.id = DLPCMD_CloseDB;
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
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	DLPC_TRACE(10, "DlpCloseDB: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_CloseDB, &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
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
DlpDeleteDB(struct PConnection *pconn,		/* Connection to Palm */
	    const int card,	/* Memory card */
	    const char *name)	/* Name of the database */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	static ubyte outbuf[128];	/* Output buffer */
					/* XXX - Fixed size: bad! */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPC_TRACE(1, ">>> DeleteDB: card %d, name \"%s\"\n",
		   card, name);

	/* Fill in the header values */
	header.id = DLPCMD_DeleteDB;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, card);
	put_ubyte(&wptr, 0);		/* Padding */
	/* XXX - Potential buffer overrun: check length of 'name' */
	memcpy(wptr, name, strlen(name));
	wptr += strlen(name);
	put_ubyte(&wptr, 0);		/* Terminating NUL */

	/* Fill in the argument */
	argv[0].id = DLPARG_DeleteDB_DB;
	argv[0].size = wptr - outbuf;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	DLPC_TRACE(10, "DlpDeleteDB: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_DeleteDB, &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
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
DlpReadAppBlock(struct PConnection *pconn,	/* Connection */
		const ubyte handle,	/* Database handle */
		const uword offset,	/* Where to start reading */
		const uword len,	/* Max # bytes to read */
		uword *size,	/* # bytes read returned here */
		const ubyte **data)
				/* Set to the data read */
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

	DLPC_TRACE(1, ">>> ReadAppBlock\n");

	/* Fill in the header values */
	header.id = DLPCMD_ReadAppBlock;
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
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	DLPC_TRACE(10, "DlpReadAppBlock: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_ReadAppBlock,
			    &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		rptr = ret_argv[i].data;
		switch (ret_argv[i].id)
		{
		    case DLPRET_ReadAppBlock_Blk:
			/* XXX - Sanity check: Make sure argv[i].size <= size */
			*size = get_uword(&rptr);
			/* Return a pointer to the data. */
			*data = rptr;
			rptr += *size;

			DLPC_TRACE(3, "block size: %d (0x%04x)\n", *size,
				   *size);
/*  			debug_dump(stderr, "APP: ", *data, *size); */

			break;
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
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
DlpWriteAppBlock(struct PConnection *pconn,	/* Connection */
		 const ubyte handle,	/* Database handle */
		 const uword offset,	/* Offset at which to start writing */
		 const uword len,	/* Length of data */
		 const ubyte *data)	/* The data to write */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	static ubyte outbuf[1024];	/* Output buffer */
					/* XXX - Fixed size: bad */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPC_TRACE(1, ">>> WriteAppBlock\n");

if (DLPARGLEN_WriteAppBlock_Block + len > 1024)
{
fprintf(stderr, "##### I can't send this AppInfo block: it's too big\n");
return -1;
}
	/* Fill in the header values */
	header.id = DLPCMD_WriteAppBlock;
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
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	/* Get a response */
	DLPC_TRACE(10, "DlpWriteAppBlock: waiting for response\n");
	err = dlp_recv_resp(pconn, DLPCMD_WriteAppBlock, &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

/* DlpReadSortBlock
 * Read the sort block for a database.
 * XXX - I think this is used by the application to sort its records.
 * XXX - Not terribly well-tested.
 */
int
DlpReadSortBlock(struct PConnection *pconn,	/* Connection */
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

	DLPC_TRACE(1, ">>> ReadSortBlock\n");

	/* Fill in the header values */
	header.id = DLPCMD_ReadSortBlock;
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
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	/* Get a response */
	DLPC_TRACE(10, "DlpReadSortBlock: waiting for response\n");
	err = dlp_recv_resp(pconn, DLPCMD_ReadSortBlock,
			    &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    case DLPRET_ReadSortBlock_Blk:
			/* XXX - Sanity check: Make sure argv[i].size <= size */
			/* Return the data and its length to the caller */
			*size = ret_argv[i].size;
			/* Return a pointer to the data */
			*data = rptr;
			rptr += *size;
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
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
DlpWriteSortBlock(struct PConnection *pconn,	/* Connection to Palm */
		  const ubyte handle,	/* Database handle */
		  const uword offset,	/* Offset at which to start writing */
		  const uword len,	/* Length of data */
		  const ubyte *data)	/* The data to write */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	static ubyte outbuf[1024];	/* Output buffer */
					/* XXX - Fixed size: bad */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPC_TRACE(1, ">>> WriteSortBlock\n");
if (DLPARGLEN_WriteSortBlock_Block + len > 1024)
{
fprintf(stderr, "##### I can't send this sort block: it's too big\n");
return -1;
}

	/* Fill in the header values */
	header.id = DLPCMD_WriteSortBlock;
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
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	/* Get a response */
	DLPC_TRACE(10, "DlpWriteSortBlock: waiting for response\n");
	err = dlp_recv_resp(pconn, DLPCMD_WriteSortBlock,
			    &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
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
	struct PConnection *pconn,	/* Connection to Palm */
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

	DLPC_TRACE(1, ">>> ReadNextModifiedRec: db %d\n", handle);

	/* Fill in the header values */
	header.id = DLPCMD_ReadNextModifiedRec;
	header.argc = 1;

	/* Fill in the argument */
	argv[0].id = DLPARG_ReadNextModifiedRec_Req;
	argv[0].size = DLPARGLEN_ReadNextModifiedRec_Req;
	argv[0].data = (void *) &handle;

	/* Send the DLP request */
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	DLPC_TRACE(10, "DlpReadNextModifiedRec: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_ReadNextModifiedRec,
			    &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

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

			DLPC_TRACE(6, "Read a record (by ID):\n");
			DLPC_TRACE(6, "\tID == 0x%08lx\n", recinfo->id);
			DLPC_TRACE(6, "\tindex == 0x%04x\n", recinfo->index);
			DLPC_TRACE(6, "\tsize == 0x%04x\n", recinfo->size);
			DLPC_TRACE(6, "\tattributes == 0x%02x\n",
				   recinfo->attributes);
			DLPC_TRACE(6, "\tcategory == 0x%02x\n",
				   recinfo->category);
#ifdef DLPC_DEBUG
			if (dlpc_debug >= 10)
				debug_dump(stderr, "REC",
					   *data,
					   recinfo->size);
#endif	/* DLPC_DEBUG */
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

int
DlpReadRecordByID(struct PConnection *pconn,	/* Connection to Palm */
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

	DLPC_TRACE(1, ">>> ReadRecord ByID: handle %d, recid %ld, offset %d, "
		   "len %d\n",
		   handle, id, offset, len);

	/* Fill in the header values */
	header.id = DLPCMD_ReadRecord;
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
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	DLPC_TRACE(10, "DlpReadRecordByID: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_ReadRecord, &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

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

			DLPC_TRACE(6, "Read a record (by ID):\n");
			DLPC_TRACE(6, "\tID == 0x%08lx\n", recinfo->id);
			DLPC_TRACE(6, "\tindex == 0x%04x\n", recinfo->index);
			DLPC_TRACE(6, "\tsize == 0x%04x\n", recinfo->size);
			DLPC_TRACE(6, "\tattributes == 0x%02x\n",
				   recinfo->attributes);
			DLPC_TRACE(6, "\tcategory == 0x%02x\n",
				   recinfo->category);
#ifdef DLPC_DEBUG
			DLPC_TRACE(10, "\tdata:\n");
			if (dlpc_debug >= 10)
				debug_dump(stderr, "RR", *data, recinfo->size);
#endif	/* DLPC_DEBUG */
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
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
 * XXX - Is there some deep significance to this?
 */
int
DlpReadRecordByIndex(
	struct PConnection *pconn,	/* Connection to Palm */
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

	DLPC_TRACE(1, ">>> ReadRecord ByIndex: handle %d, index %d\n",
		   handle,
		   index);

	/* Fill in the header values */
	header.id = DLPCMD_ReadRecord;
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
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	DLPC_TRACE(10, "DlpReadRecordByIndex: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_ReadRecord, &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

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

			DLPC_TRACE(6, "Read a record (by index):\n");
			DLPC_TRACE(6, "\tID == 0x%08lx\n", recinfo->id);
			DLPC_TRACE(6, "\tindex == 0x%04x\n", recinfo->index);
			DLPC_TRACE(6, "\tsize == 0x%04x\n", recinfo->size);
			DLPC_TRACE(6, "\tattributes == 0x%02x\n",
				   recinfo->attributes);
			DLPC_TRACE(6, "\tcategory == 0x%02x\n",
				   recinfo->category);
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
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
DlpWriteRecord(struct PConnection *pconn,	/* Connection to Palm */
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
	static ubyte outbuf[2048];	/* Output buffer */
					/* XXX - Fixed size: bad! */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPC_TRACE(1, ">>> WriteRecord: handle %d, flags 0x%02x, "
		   "recid 0x%08lx, attr 0x%02x, category %d, len %ld\n",
		   handle,
		   flags,
		   id,
		   attributes,
		   category,
		   len);
	DLPC_TRACE(10, "Raw record data (%ld == 0x%04lx bytes):\n",
		   len, len);
#ifdef DLPC_DEBUG
	if (dlpc_debug >= 10)
		debug_dump(stderr, "WR", data, len);
#endif	/* DLPC_DEBUG */

	/* Fill in the header values */
	header.id = DLPCMD_WriteRecord;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, handle);
	put_ubyte(&wptr, flags);
		/* XXX - Flags: the Palm header says that the high bit
		 * (0x80) should always be set. make sure it is.
		 */
	put_udword(&wptr, id);
	put_ubyte(&wptr, attributes);
		/* XXX - Attributes: under early versions of the protocol,
		 * only the "secret" attribute is allowed. As of 1.1,
		 * "deleted", "archived" and "dirty" are also allowed.
		 * Check these and clear the forbidden bits.
		 */
	put_ubyte(&wptr, category);
	/* XXX - Potential buffer overflow. */
	memcpy(wptr, data, len);
	wptr += len;

	/* Fill in the argument */
	argv[0].id = DLPARG_WriteRecord_Rec;
	argv[0].size = wptr - outbuf;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	DLPC_TRACE(10, "DlpWriteRecord: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_WriteRecord,
			    &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

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
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
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
DlpDeleteRecord(struct PConnection *pconn,	/* Connection to Palm */
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

	DLPC_TRACE(1, ">>> DeleteRecord: handle %d, flags 0x%02x, "
		   "recid 0x%08lx\n",
		   handle, flags, recid);

	/* Fill in the header values */
	header.id = DLPCMD_DeleteRecord;
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
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	DLPC_TRACE(10, "DlpDeleteRecord: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_DeleteRecord, &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

int
DlpReadResourceByIndex(
	struct PConnection *pconn,	/* Connection to Palm */
	const ubyte handle,		/* Database handle */
	const uword index,		/* Resource index */
	const uword offset,		/* Offset into resource */
	const uword len,		/* #bytes to read (~0n == to the
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
	static ubyte outbuf[2048];	/* Output buffer */
					/* XXX - Fixed size: bad! */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPC_TRACE(1, ">>> ReadResourceByIndex: handle %d, index %d, "
		   "offset %d, len %d\n",
		   handle, index, offset, len);

	/* Fill in the header values */
	header.id = DLPCMD_ReadResource;
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
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	DLPC_TRACE(10, "DlpReadResourceByIndex: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_ReadResource, &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

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
			/* XXX - Potential buffer overflow */
/*  			memcpy(data, rptr, value->size); */
/*  			rptr += value->size; */
*data = rptr;

			DLPC_TRACE(3, "Resource: type '%c%c%c%c' (0x%08lx), "
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
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

int
DlpReadResourceByType(
	struct PConnection *pconn,	/* Connection to Palm */
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
	static ubyte outbuf[2048];	/* Output buffer */
					/* XXX - Fixed size: bad! */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPC_TRACE(1, ">>> ReadResourceByType: handle %d, type %ld, id %d, "
		   "offset %d, len %d\n",
		   handle, type, id, offset, len);

	/* Fill in the header values */
	header.id = DLPCMD_ReadResource;
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
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	DLPC_TRACE(10, "DlpReadResourceByType: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_ReadResource, &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

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
			/* XXX - Potential buffer overflow */
			memcpy(data, rptr, value->size);
			rptr += value->size;

			DLPC_TRACE(3, "Resource: type '%c%c%c%c' (0x%08lx), "
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
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

int
DlpWriteResource(struct PConnection *pconn,	/* Connection to Palm */
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
	ubyte *outbuf;
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPC_TRACE(1, "WriteResource: type '%c%c%c%c' (0x%08lx), id %d, "
		   "size %d\n",
		   (char) (type >> 24) & 0xff,
		   (char) (type >> 16) & 0xff,
		   (char) (type >> 8) & 0xff,
		   (char) type & 0xff,
		   type,
		   id, size);

	/* Fill in the header values */
	header.id = DLPCMD_WriteResource;
	header.argc = 1;

	/* Construct the argument */
	if ((outbuf = (ubyte *) malloc(DLPARGLEN_WriteResource_Rsrc+size))
	    == NULL)
	{
		fprintf(stderr, "Out of memory\n");
		return -1;
	}
	wptr = outbuf;
	put_ubyte(&wptr, handle);
	put_ubyte(&wptr, 0);		/* Padding */
	put_udword(&wptr, type);
	put_uword(&wptr, id);
	put_uword(&wptr, size);
	/* XXX - Potential buffer overflow */
	memcpy(wptr, data, size);
	wptr += size;

	/* Fill in the argument */
	argv[0].id = DLPARG_WriteResource_Rsrc;
	argv[0].size = wptr - outbuf;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
	{
		free(outbuf);
		return err;
	}

	DLPC_TRACE(10, "DlpWriteResource: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_WriteResource,
			    &resp_header, &ret_argv);
	if (err < 0)
	{
		free(outbuf);
		return err;
	}

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
	{
		free(outbuf);
		return resp_header.errno;
	}

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
				ret_argv[i].id);
			continue;
		}
	}

	free(outbuf);
	return 0;		/* Success */
}

int
DlpDeleteResource(struct PConnection *pconn,	/* Connection to Palm */
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

	DLPC_TRACE(1, ">>> DeleteResource: handle %d, flags 0x%02x, "
		   "type '%c%c%c%c' (0x%08lx), id %d\n",
		   handle, flags,
		   (char) (type >> 24) & 0xff,
		   (char) (type >> 16) & 0xff,
		   (char) (type >> 8) & 0xff,
		   (char) type & 0xff,
		   type,
		   id);

	/* Fill in the header values */
	header.id = DLPCMD_DeleteResource;
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
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	DLPC_TRACE(10, "DlpDeleteResource: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_DeleteResource,
			    &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
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
	struct PConnection *pconn,	/* Connection to Palm */
	const ubyte handle)		/* Database handle */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */

	DLPC_TRACE(1, ">>> CleanUpDatabase: handle %d\n", handle);

	/* Fill in the header values */
	header.id = DLPCMD_CleanUpDatabase;
	header.argc = 1;

	/* Fill in the argument */
	argv[0].id = DLPARG_CleanUpDatabase_DB;
	argv[0].size = DLPARGLEN_CleanUpDatabase_DB;
	argv[0].data = (void *) &handle;

	/* Send the DLP request */
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	DLPC_TRACE(10, "DlpCleanUpDatabase: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_CleanUpDatabase, &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
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
DlpResetSyncFlags(struct PConnection *pconn,	/* Connection to Palm */
		  const ubyte handle)		/* Database handle */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */

	DLPC_TRACE(1, ">>> ResetSyncFlags: handle %d\n", handle);

	/* Fill in the header values */
	header.id = DLPCMD_ResetSyncFlags;
	header.argc = 1;

	/* Fill in the argument */
	argv[0].id = DLPARG_ResetSyncFlags_DB;
	argv[0].size = DLPARGLEN_ResetSyncFlags_DB;
	argv[0].data = (void *) &handle;

	/* Send the DLP request */
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	DLPC_TRACE(10, "DlpResetSyncFlags: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_ResetSyncFlags,
			    &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

/* XXX - The API probably could use some work */
int
DlpCallApplication(
	struct PConnection *pconn,	/* Connection to Palm */
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
	static ubyte outbuf[2048];	/* Output buffer */
					/* XXX - Fixed size: bad! */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPC_TRACE(1, ">>> CallApplication: ver 0x%08lx, creator '%c%c%c%c' "
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
	header.id = DLPCMD_CallApplication;
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
	/* XXX - Potential buffer overrun */
	if (param != NULL)
		/* XXX - Error-checking: conceivably 'param' could be NULL
		 * and 'paramsize' > 0.
		 */
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
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	DLPC_TRACE(10, "DlpCallApplication: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_CallApplication,
			    &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

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
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
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
DlpResetSystem(struct PConnection *pconn)	/* Connection to Palm */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	const struct dlp_arg *ret_argv;		/* Response argument list */

	DLPC_TRACE(1, ">>> ResetSystem\n");

	/* Fill in the header values */
	header.id = DLPCMD_ResetSystem;
	header.argc = 0;

	/* Send the DLP request */
	err = dlp_send_req(pconn, &header, NULL);
	if (err < 0)
		return err;

	DLPC_TRACE(10, "DlpResetSystem: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_ResetSystem, &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

/* DlpAddSyncLogEntry
 *
 * It appears that only the first entry takes: subsequent ones get
 * ignored. Hence, you have to transmit your entire log at once.
 * Bleah. Oh, and just for the record: no, you can't send multiple
 * arguments, each with one line of the log. That would have been just
 * a bit too easy :-(
 */
int
DlpAddSyncLogEntry(struct PConnection *pconn,	/* Connection to Palm */
		   const char *msg)		/* Log message */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */

	DLPC_TRACE(1, ">>> AddSyncLogEntry \"%s\"\n", msg);

	/* Fill in the header values */
	header.id = DLPCMD_AddSyncLogEntry;
	header.argc = 1;

	/* Fill in the argument */
	argv[0].id = DLPARG_AddSyncLogEntry_Msg;
	argv[0].size = strlen(msg)+1;	/* Include the final NUL */
	argv[0].data = (ubyte *) msg;

	/* Send the DLP request */
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	DLPC_TRACE(10, "DlpAddSyncLogEntry: waiting for response\n");
	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_AddSyncLogEntry,
			    &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
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
DlpReadOpenDBInfo(struct PConnection *pconn,	/* Connection */
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

	DLPC_TRACE(1, ">>> ReadOpenDBInfo(%d)\n", handle);

	/* Fill in the header values */
	header.id = DLPCMD_ReadOpenDBInfo;
	header.argc = 1;

	/* Fill in the argument */
	argv[0].id = DLPARG_ReadOpenDBInfo_DB;
	argv[0].size = DLPARGLEN_ReadOpenDBInfo_DB;
	argv[0].data = &handle;

	/* Send the DLP request */
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	/* Get a response */
	DLPC_TRACE(10, "DlpReadOpenDBInfo: waiting for response\n");
	err = dlp_recv_resp(pconn, DLPCMD_ReadOpenDBInfo,
			    &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

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
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;
}

int
DlpMoveCategory(struct PConnection *pconn,	/* Connection to Palm */
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

	DLPC_TRACE(1, ">>> MoveCategory: handle %d, from %d, to %d\n",
		   handle, from, to);

	/* Fill in the header values */
	header.id = DLPCMD_MoveCategory;
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
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	DLPC_TRACE(10, "DlpMoveCategory: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_MoveCategory,
			    &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
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
DlpOpenConduit(struct PConnection *pconn)	/* Connection to Palm */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	const struct dlp_arg *ret_argv;		/* Response argument list */

	DLPC_TRACE(1, ">>> OpenConduit:\n");

	/* Fill in the header values */
	header.id = DLPCMD_OpenConduit;
	header.argc = 0;

	/* Send the DLP request */
	err = dlp_send_req(pconn, &header, NULL);
	if (err < 0)
		return err;

	DLPC_TRACE(10, "DlpOpenConduit: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_OpenConduit,
			    &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

int
DlpEndOfSync(struct PConnection *pconn,	/* Connection to Palm */
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

	DLPC_TRACE(1, ">>> EndOfSync status %d\n", status);

	/* Fill in the header values */
	header.id = DLPCMD_EndOfSync;
	header.argc = 1;

	/* Construct the status buffer */
	wptr = status_buf;
	put_uword(&wptr, status);

	/* Fill in the argument */
	argv[0].id = DLPARG_EndOfSync_Status;
	argv[0].size = DLPARGLEN_EndOfSync_Status;
	argv[0].data = status_buf;

	/* Send the DLP request */
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	/* Get a response */
	DLPC_TRACE(10, "DlpEndOfSync: waiting for response\n");
	err = dlp_recv_resp(pconn, DLPCMD_EndOfSync, &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
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
	struct PConnection *pconn,	/* Connection to Palm */
	const ubyte handle)		/* Database handle */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */

	DLPC_TRACE(1, ">>> ResetRecordIndex: handle %d\n", handle);

	/* Fill in the header values */
	header.id = DLPCMD_ResetRecordIndex;
	header.argc = 1;

	/* Fill in the argument */
	argv[0].id = DLPARG_ResetRecordIndex_DB;
	argv[0].size = DLPARGLEN_ResetRecordIndex_DB;
	argv[0].data = (void *) &handle;

	/* Send the DLP request */
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	DLPC_TRACE(10, "DlpResetRecordIndex: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_ResetRecordIndex,
			    &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

/* DlpReadRecordIDList
 * Read the list of record IDs in the database whose handle is
 * 'handle'.
 */
int
DlpReadRecordIDList(
	struct PConnection *pconn,	/* Connection to Palm */
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

	DLPC_TRACE(1, ">>> ReadRecordIDList: handle %d, flags 0x%02x, "
		   "start %d, max %d\n",
		   handle,
		   flags,
		   start,
		   max);

	/* Fill in the header values */
	header.id = DLPCMD_ReadRecordIDList;
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
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	DLPC_TRACE(10, "DlpReadRecordIDList: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_ReadRecordIDList,
			    &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		rptr = ret_argv[i].data;
		switch (ret_argv[i].id)
		{
		    case DLPRET_ReadRecordIDList_List:
			*numread = get_uword(&rptr);
			/* XXX - Make sure max >= *numread */
			for (i = 0; i < *numread; i++)
				recids[i] = get_udword(&rptr);
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
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
	struct PConnection *pconn,	/* Connection to Palm */
	const ubyte handle,		/* Database handle */
	const ubyte category,		/* Category ID */
	struct dlp_readrecret *record)	/* The record will be returned here */
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

	DLPC_TRACE(1, ">>> ReadNextRecInCategory: handle %d, category %d\n",
		   handle, category);

	/* Fill in the header values */
	header.id = DLPCMD_ReadNextRecInCategory;
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
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	DLPC_TRACE(10, "DlpReadNextRecInCategory: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_ReadNextRecInCategory,
			    &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		rptr = ret_argv[i].data;
		switch (ret_argv[i].id)
		{
		    case DLPRET_ReadNextRecInCategory_Rec:
			record->recid = get_udword(&rptr);
			record->index = get_uword(&rptr);
			record->size = get_uword(&rptr);
			record->attributes = get_ubyte(&rptr);
			record->category = get_ubyte(&rptr);
			record->data = rptr;

			DLPC_TRACE(6, "Read record in category %d:\n",
				   category);
			DLPC_TRACE(6, "\tID == 0x%08lx\n", record->recid);
			DLPC_TRACE(6, "\tindex == 0x%04x\n", record->index);
			DLPC_TRACE(6, "\tsize == 0x%04x\n", record->size);
			DLPC_TRACE(6, "\tattributes == 0x%02x\n",
				   record->attributes);
			DLPC_TRACE(6, "\tcategory == 0x%02x\n",
				   record->category);
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
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
	struct PConnection *pconn,	/* Connection to Palm */
	const ubyte handle,		/* Database handle */
	const ubyte category,		/* Category ID */
	struct dlp_readrecret *record)	/* The record will be returned here */
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

	DLPC_TRACE(1, ">>> ReadNextModifiedRecInCategory: handle %d, "
		   "category %d\n",
		   handle, category);

	/* Fill in the header values */
	header.id = DLPCMD_ReadNextModifiedRecInCategory;
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
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	DLPC_TRACE(10, "DlpReadNextModifiedRecInCategory: waiting for "
		   "response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_ReadNextModifiedRecInCategory,
			    &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		rptr = ret_argv[i].data;
		switch (ret_argv[i].id)
		{
		    case DLPRET_ReadNextModifiedRecInCategory_Rec:
			record->recid = get_udword(&rptr);
			record->index = get_uword(&rptr);
			record->size = get_uword(&rptr);
			record->attributes = get_ubyte(&rptr);
			record->category = get_ubyte(&rptr);
			record->data = rptr;

			DLPC_TRACE(6, "Read record in category %d:\n",
				   category);
			DLPC_TRACE(6, "\tID == 0x%08lx\n", record->recid);
			DLPC_TRACE(6, "\tindex == 0x%04x\n", record->index);
			DLPC_TRACE(6, "\tsize == 0x%04x\n", record->size);
			DLPC_TRACE(6, "\tattributes == 0x%02x\n",
				   record->attributes);
			DLPC_TRACE(6, "\tcategory == 0x%02x\n",
				   record->category);
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
				ret_argv[i].id);
			continue;
		}
	}

	return 0;		/* Success */
}

/* XXX - This is untested: I don't know what values to feed it */
int
DlpReadAppPreference(
	struct PConnection *pconn,	/* Connection to Palm */
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

	DLPC_TRACE(1, ">>> ReadAppPreference: creator '%c%c%c%c' (0x%08lx), "
		   "id %d, len %d, flags 0x%02x\n",
		   (char) (creator >> 24) & 0xff,
		   (char) (creator >> 16) & 0xff,
		   (char) (creator >> 8) & 0xff,
		   (char) creator & 0xff,
		   creator,
		   id, len, flags);

	/* Fill in the header values */
	header.id = DLPCMD_ReadAppPreference;
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
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	DLPC_TRACE(10, "DlpReadAppPreference: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_ReadAppPreference,
			    &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

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
			/* XXX - Potential buffer overflow */
			memcpy(data, rptr, pref->len);
			rptr += pref->len;

			DLPC_TRACE(3, "Read an app. preference: version %d, "
				   "size %d, len %d\n",
				   pref->version, pref->size, pref->len);
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
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
	struct PConnection *pconn,	/* Connection to Palm */
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
	static ubyte outbuf[2048];	/* Output buffer */
					/* XXX - Fixed size: bad! */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPC_TRACE(1, ">>> WriteAppPreference: XXX\n");

	/* Fill in the header values */
	header.id = DLPCMD_WriteAppPreference;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_udword(&wptr, creator);
	put_uword(&wptr, id);
	put_uword(&wptr, pref->version);
	put_uword(&wptr, pref->size);
	put_ubyte(&wptr, flags);
	put_ubyte(&wptr, 0);		/* Padding */
	/* XXX - Potential buffer overflow */
	memcpy(outbuf, data, pref->size);
	wptr += pref->size;

	/* Fill in the argument */
	argv[0].id = DLPARG_WriteAppPreference_Pref;
	argv[0].size = wptr - outbuf;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	DLPC_TRACE(10, "DlpWriteRecord: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_WriteAppPreference,
			    &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
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
DlpReadNetSyncInfo(struct PConnection *pconn,
		   struct dlp_netsyncinfo *netsyncinfo)
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */

	DLPC_TRACE(1, ">>> ReadNetSyncInfo\n");

	/* Fill in the header values */
	header.id = DLPCMD_ReadNetSyncInfo;
	header.argc = 0;

	/* Send the DLP request */
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	DLPC_TRACE(10, "DlpReadNetSyncInfo: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_ReadNetSyncInfo,
			    &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

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
			/* XXX - Possible buffer overflows */
			memcpy(netsyncinfo->synchostname, rptr,
			       netsyncinfo->hostnamesize);
			rptr += netsyncinfo->hostnamesize;
			memcpy(netsyncinfo->synchostaddr, rptr,
			       netsyncinfo->hostaddrsize);
			rptr += netsyncinfo->hostaddrsize;
			memcpy(netsyncinfo->synchostnetmask, rptr,
			       netsyncinfo->hostnetmasksize);
			rptr += netsyncinfo->hostnetmasksize;

			DLPC_TRACE(6, "NetSync info:\n");
			DLPC_TRACE(6, "\tLAN sync: %d\n",
				   netsyncinfo->lansync_on);
			DLPC_TRACE(6, "\thostname: (%d) \"%s\"\n",
				   netsyncinfo->hostnamesize,
				   netsyncinfo->synchostname);
			DLPC_TRACE(6, "\taddress: (%d) \"%s\"\n",
				   netsyncinfo->hostaddrsize,
				   netsyncinfo->synchostaddr);
			DLPC_TRACE(6, "\tnetmask: (%d) \"%s\"\n",
				   netsyncinfo->hostnetmasksize,
				   netsyncinfo->synchostnetmask);
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
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
 * XXX - Check to make sure the Palm understands v1.1 of the protocol.
 */
int
DlpWriteNetSyncInfo(struct PConnection *pconn,	/* Connection to Palm */
		    const struct dlp_writenetsyncinfo *netsyncinfo)
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Request argument list */
	const struct dlp_arg *ret_argv;	/* Response argument list */
	static ubyte outbuf[2048];	/* Output buffer */
					/* XXX - Fixed size: bad! */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPC_TRACE(1, ">>> WriteNetSyncInfo: mod 0x%02x, LAN %d, name (%d) "
		   "\"%s\", addr (%d) \"%s\", mask (%d) \"%s\"\n",
		   netsyncinfo->modflags,
		   netsyncinfo->netsyncinfo.lansync_on,
		   netsyncinfo->netsyncinfo.hostnamesize,
		   netsyncinfo->netsyncinfo.synchostname,
		   netsyncinfo->netsyncinfo.hostaddrsize,
		   netsyncinfo->netsyncinfo.synchostaddr,
		   netsyncinfo->netsyncinfo.hostnetmasksize,
		   netsyncinfo->netsyncinfo.synchostnetmask);

	/* Fill in the header values */
	header.id = DLPCMD_WriteNetSyncInfo;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, netsyncinfo->modflags);
	put_ubyte(&wptr, netsyncinfo->netsyncinfo.lansync_on);
	put_udword(&wptr, 0);		/* reserved1b */
	put_udword(&wptr, 0);		/* reserved2 */
	put_udword(&wptr, 0);		/* reserved3 */
	put_udword(&wptr, 0);		/* reserved4 */
	/* XXX - Should these use strlen()? */
	/* XXX - Potential buffer overruns */
	put_uword(&wptr, netsyncinfo->netsyncinfo.hostnamesize);
	put_uword(&wptr, netsyncinfo->netsyncinfo.hostaddrsize);
	put_uword(&wptr, netsyncinfo->netsyncinfo.hostnetmasksize);
	memcpy(wptr, netsyncinfo->netsyncinfo.synchostname,
	       netsyncinfo->netsyncinfo.hostnamesize);
	wptr += netsyncinfo->netsyncinfo.hostnamesize;
	memcpy(wptr, netsyncinfo->netsyncinfo.synchostaddr,
	       netsyncinfo->netsyncinfo.hostaddrsize);
	wptr += netsyncinfo->netsyncinfo.hostaddrsize;
	memcpy(wptr, netsyncinfo->netsyncinfo.synchostnetmask,
	       netsyncinfo->netsyncinfo.hostnetmasksize);
	wptr += netsyncinfo->netsyncinfo.hostnetmasksize;

	/* Fill in the argument */
	argv[0].id = DLPARG_WriteNetSyncInfo_Info;
	argv[0].size = wptr - outbuf;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	DLPC_TRACE(10, "DlpWriteNetSyncInfo: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_WriteNetSyncInfo,
			    &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		switch (ret_argv[i].id)
		{
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
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
DlpReadFeature(struct PConnection *pconn,	/* Connection to Palm */
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

	DLPC_TRACE(1, ">>> ReadFeature: creator '%c%c%c%c' (0x%08lx), number %d\n",
		   (char) (creator >> 24) & 0xff,
		   (char) (creator >> 16) & 0xff,
		   (char) (creator >> 8) & 0xff,
		   (char) creator & 0xff,
		   creator,
		   featurenum);

	/* Fill in the header values */
	header.id = DLPCMD_ReadFeature;
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
	err = dlp_send_req(pconn, &header, argv);
	if (err < 0)
		return err;

	DLPC_TRACE(10, "DlpReadFeature: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(pconn, DLPCMD_ReadFeature,
			    &resp_header, &ret_argv);
	if (err < 0)
		return err;

	DLPC_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		   resp_header.id,
		   resp_header.argc,
		   resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		return resp_header.errno;

	/* Parse the argument(s) */
	for (i = 0; i < resp_header.argc; i++)
	{
		rptr = ret_argv[i].data;
		switch (ret_argv[i].id)
		{
		    case DLPRET_ReadFeature_Feature:
			*value = get_udword(&rptr);

			DLPC_TRACE(3, "Read feature: 0x%08lx (%ld)\n",
				   *value, *value);
			break;
		    default:	/* Unknown argument type */
			fprintf(stderr, "##### Unknown argument type: 0x%02x\n",
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
