/* dlp_cmd.c
 *
 * Functions for manipulating a remote Palm device via the Desktop
 * Link Protocol (DLP).
 *
 * The functions in this file constitute the programmer-visible DLP
 * interface. They package their arguments up, pass them on to the DLP
 * protocol functions, interpret their results, and repackage them
 * back for return to the caller.
 *
 * $Id: dlp_cmd.c,v 1.1 1999-02-19 22:51:54 arensb Exp $
 */
#include <stdio.h>
#include "dlp.h"
#include "dlp_cmd.h"
#include "util.h"
#include "palm_errno.h"

#define DLPCMD_DEBUG	1
#ifdef DLPCMD_DEBUG
int dlpcmd_debug = 0;

#define DLPCMD_TRACE(level, format...)		\
	if (dlpcmd_debug >= (level))		\
		fprintf(stderr, "DLPC:" format)

#endif	/* DLPCMD_DEBUG */

/* dlpcmd_gettime
 * Just a convenience function for reading a Palm date from a data
 * buffer.
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

	/* XXX - If the year is 0, then this is really a nonexistent
	 * date. Presumably, then the other fields should be set to 0
	 * as well.
	 */
}

/* dlpcmd_puttime
 * Just a convenience function for writing a Palm date to a data
 * buffer.
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

/* XXX - Perhaps there ought to be functions for converting between
 * 'time_t's and 'dlp_time's? */

int
DlpReadUserInfo(int fd,		/* File descriptor */
		struct dlp_userinfo *userinfo)
				/* Will be filled in with user
				 * information. */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];	/* Response argument list */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */

	DLPCMD_TRACE(1, ">>> ReadUserInfo\n");

	/* Fill in the header values */
	header.id = DLPCMD_ReadUserInfo;
	header.argc = 0;

	/* Send the DLP request */
	err = dlp_send_req(fd, &header, NULL);
	if (err < 0)
		return err;	/* Error */

	DLPCMD_TRACE(10, "DlpReadUserInfo: waiting for response\n");
	/* Get a response */
	err = dlp_recv_resp(fd, DLPCMD_ReadUserInfo, &resp_header, 1, argv);
	if (err < 0)
		return err;	/* Error */

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);

	/* Parse the argument */
	/* XXX - Make sure there's exactly one argument */
	/* XXX - Make sure the argument(s) are sane: size, type */
	rptr = argv[0].data;
	userinfo->userid = get_udword(&rptr);
	userinfo->viewerid = get_udword(&rptr);
	userinfo->lastsyncPC = get_udword(&rptr);
	dlpcmd_gettime(&rptr, &(userinfo->lastgoodsync));
	dlpcmd_gettime(&rptr, &(userinfo->lastsync));
	userinfo->usernamelen = get_ubyte(&rptr);
	userinfo->passwdlen = get_ubyte(&rptr);
	/* XXX - Potential buffer overflow */
	memcpy(userinfo->username, rptr, userinfo->usernamelen);
	rptr += userinfo->usernamelen;
	/* XXX - Potential buffer overflow */
	memcpy(userinfo->passwd, rptr, userinfo->passwdlen);
	rptr += userinfo->passwdlen;

	DLPCMD_TRACE(1, "Got user info: user 0x%08lx, viewer 0x%08lx, last PC 0x%08lx\n",
		     userinfo->userid,
		     userinfo->viewerid,
		     userinfo->lastsyncPC);
	DLPCMD_TRACE(1, "Last successful sync %02d:%02d:%02d, %d/%d/%d\n",
		     userinfo->lastgoodsync.hour,
		     userinfo->lastgoodsync.minute,
		     userinfo->lastgoodsync.second,
		     userinfo->lastgoodsync.day,
		     userinfo->lastgoodsync.month,
		     userinfo->lastgoodsync.year);
	DLPCMD_TRACE(1, "Last sync attempt %02d:%02d:%02d, %d/%d/%d\n",
		     userinfo->lastsync.hour,
		     userinfo->lastsync.minute,
		     userinfo->lastsync.second,
		     userinfo->lastsync.day,
		     userinfo->lastsync.month,
		     userinfo->lastsync.year);
	DLPCMD_TRACE(1, "User name: (%d bytes) \"%*s\"\n",
		     userinfo->usernamelen,
		     userinfo->usernamelen-1,
		     userinfo->username);
#if DLPCMD_DEBUG
	if (dlpcmd_debug >= 1)
	{
		fprintf(stderr, "DLPC: Password (%d bytes):\n",
			userinfo->passwdlen);
		debug_dump(stderr, "DLPC:", userinfo->passwd, userinfo->passwdlen);
	}
#endif	/* DLPCMD_DEBUG */

	return 0;		/* Success */
}

int
DlpWriteUserInfo(int fd,	/* File descriptor */
		 const struct dlp_setuserinfo *userinfo)
				/* Bits of user information to set */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Argument list */
	static ubyte outbuf[DLPARGLEN_WriteUserInfo_UserInfo +
		DLPCMD_USERNAME_LEN];	/* Buffer holding outgoing arg */
					/* XXX - Fixed size: bad!
					 * (OTOH, the maximum size is
					 * pretty much determined.) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPCMD_TRACE(1, ">>> WriteUserInfo\n");

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
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(10, "DlpWriteUserInfo: waiting for response\n");
	/* Get a response */
	err = dlp_recv_resp(fd, DLPCMD_WriteUserInfo, &resp_header, 0, NULL);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);

	/* XXX - Check number of response arguments */

	return 0;		/* Success */
}

int
DlpReadSysInfo(int fd,		/* File descriptor */
	       struct dlp_sysinfo *sysinfo)
				/* Will be filled in with system
				 * information */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Response argument list */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */

	DLPCMD_TRACE(1, ">>> ReadSysInfo\n");

	/* Fill in the header values */
	header.id = DLPCMD_ReadSysInfo;
	header.argc = 0;

	/* Send the DLP request */
	err = dlp_send_req(fd, &header, NULL);
	if (err < 0)
		return err;

	/* Get a response */
	err = dlp_recv_resp(fd, DLPCMD_ReadSysInfo, &resp_header, 1, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);

	/* XXX - Check number of response arguments */
	/* Parse the argument */
	rptr = argv[0].data;
	sysinfo->rom_version = get_udword(&rptr);
	sysinfo->localization = get_udword(&rptr);
	get_ubyte(&rptr);		/* Skip padding */
	sysinfo->prodIDsize = get_ubyte(&rptr);
	sysinfo->prodID = get_udword(&rptr);

	DLPCMD_TRACE(1, "Got sysinfo: ROM version 0x%08lx, loc. 0x%08lx, pIDsize %d, pID 0x%08lx\n",
		     sysinfo->rom_version,
		     sysinfo->localization,
		     sysinfo->prodIDsize,
		     sysinfo->prodID);

	return 0;		/* Success */
}

int
DlpGetSysDateTime(int fd,
		  struct dlp_time *ptime)
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Response argument list */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */

	DLPCMD_TRACE(1, ">>> GetSysDateTime\n");

	/* Fill in the header values */
	header.id = DLPCMD_GetSysDateTime;
	header.argc = 0;

	/* Send the DLP request */
	err = dlp_send_req(fd, &header, NULL);
	if (err < 0)
		return err;

	err = dlp_recv_resp(fd, DLPCMD_GetSysDateTime, &resp_header, 1, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);

	/* XXX - Check number of response arguments */
	/* Parse the response argument */
	rptr = argv[0].data;
	ptime->year = get_uword(&rptr);
	ptime->month = get_ubyte(&rptr);
	ptime->day = get_ubyte(&rptr);
	ptime->hour = get_ubyte(&rptr);
	ptime->minute = get_ubyte(&rptr);
	ptime->second = get_ubyte(&rptr);
	get_ubyte(&rptr);		/* Skip padding */

	DLPCMD_TRACE(1, "System time: %02d:%02d:%02d, %d/%d/%d\n",
		     ptime->hour,
		     ptime->minute,
		     ptime->second,
		     ptime->day,
		     ptime->month,
		     ptime->year);

	return 0;		/* Success */
}

int
DlpSetSysDateTime(int fd,	/* File descriptor */
		  const struct dlp_time *ptime)
				/* Time to set */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Argument list */
	static ubyte outbuf[DLPARGLEN_SetSysDateTime_Time];
					/* Buffer holding outgoing arg */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPCMD_TRACE(1, ">>> SetSysDateTime(%02d:%02d:%02d, %d/%d/%d)\n",
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
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(10, "DlpSetSysDateTime: waiting for response\n");
	/* Get a response */
	err = dlp_recv_resp(fd, DLPCMD_SetSysDateTime, &resp_header, 0, NULL);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);

	return err;	/* XXX */
}

int
DlpReadStorageInfo(int fd,
		   int card)
{
	/* XXX - This whole function needs to be rewritten once I have
	 * some idea what's going on.
	 */
	int err;
	struct dlp_req_header header;
	struct dlp_resp_header resp_header;
	struct dlp_arg argv[2];
struct dlp_storageinfo sinfo;
struct dlp_cardinfo cinfo;
struct dlp_cardinfo_ext extinfo;
	static ubyte outbuf[2048];
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPCMD_TRACE(1, ">>> ReadStorageInfo(%d)\n", card);

	header.id = DLPCMD_ReadStorageInfo;
	header.argc = 1;

	wptr = outbuf;
	put_ubyte(&wptr, card);		/* Read 0th card */
	put_ubyte(&wptr, 0);		/* Padding */

	argv[0].id = DLPARG_BASE;
	argv[0].size = 2;
	argv[0].data = outbuf;

	err = dlp_send_req(fd, &header, argv);
if (err < 0) return err;

	err = dlp_recv_resp(fd, DLPCMD_ReadStorageInfo, &resp_header, 2, argv);
if (err < 0) return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Should do something smart, like set dlpcmd_errno
		 * to 'resp_header.errno', and return -1 or something.
		 */
		return -resp_header.errno;

rptr = argv[0].data;
sinfo.lastcard = get_ubyte(&rptr);
sinfo.more = get_ubyte(&rptr);
/* XXX - sinfo.more is set to 1 in all cases, but none of the
 * subsequent calls to ReadStorageInfo return anything meaningful,
 * either with xcopilot or on a real PalmPilot.
 */ 
get_ubyte(&rptr);	/* Padding */
sinfo.act_count = get_ubyte(&rptr);

cinfo.totalsize = get_ubyte(&rptr);
cinfo.cardno = get_ubyte(&rptr);
cinfo.cardversion = get_uword(&rptr);
cinfo.ctime.year = get_uword(&rptr);
cinfo.ctime.month = get_ubyte(&rptr);
cinfo.ctime.day = get_ubyte(&rptr);
cinfo.ctime.hour = get_ubyte(&rptr);
cinfo.ctime.minute = get_ubyte(&rptr);
cinfo.ctime.second = get_ubyte(&rptr);
get_ubyte(&rptr);
cinfo.rom_size = get_udword(&rptr);
cinfo.ram_size = get_udword(&rptr);
cinfo.free_ram = get_udword(&rptr);
cinfo.cardname_size = get_ubyte(&rptr);
cinfo.manufname_size = get_ubyte(&rptr);
cinfo.cardname = rptr;
rptr += cinfo.cardname_size;
cinfo.manufname = rptr;
rptr += cinfo.manufname_size;

if (resp_header.argc >= 2)
{
rptr = argv[1].data;
extinfo.rom_dbs = get_uword(&rptr);
extinfo.ram_dbs = get_uword(&rptr);
}

fprintf(stderr, "GetStorageInfo:\n");
fprintf(stderr, "\tlastcard: %d\n", sinfo.lastcard);
fprintf(stderr, "\tmore: %d\n", sinfo.more);
fprintf(stderr, "\tact_count: %d\n", sinfo.act_count);
fprintf(stderr, "\n");

fprintf(stderr, "\ttotalsize == %d\n", cinfo.totalsize);
fprintf(stderr, "\tcardno == %d\n", cinfo.cardno);
fprintf(stderr, "\tcardversion == %d\n", cinfo.cardversion);
fprintf(stderr, "\tctime == %02d:%02d:%02d, %d/%d/%d\n",
	cinfo.ctime.hour,
	cinfo.ctime.minute,
	cinfo.ctime.second,
	cinfo.ctime.day,
	cinfo.ctime.month,
	cinfo.ctime.year);
fprintf(stderr, "\tROM: %ld, RAM: %ld, free RAM: %ld\n",
	cinfo.rom_size,
	cinfo.ram_size,
	cinfo.free_ram);
fprintf(stderr, "\tcardname (%d): \"%*s\"\n",
	cinfo.cardname_size,
	cinfo.cardname_size,
	cinfo.cardname);
fprintf(stderr, "\tmanufname (%d): \"%*s\"\n",
	cinfo.manufname_size,
	cinfo.manufname_size,
	cinfo.manufname);
fprintf(stderr, "\n\tROM dbs: %d\tRAM dbs: %d\n",
	extinfo.rom_dbs,
	extinfo.ram_dbs);

	return err;
}

int
DlpReadDBList(int fd,		/* File descriptor */
	      ubyte flags,	/* Search flags */
	      int card,		/* Card number */
	      uword start)	/* Database index to start at */
{
	int err;
	struct dlp_req_header header;
	struct dlp_resp_header resp_header;
	struct dlp_arg argv[1];
struct dlp_dblist_header list_header;
struct dlp_dbinfo dbinfo;
static ubyte outbuf[2048]; 
	const ubyte *rptr;	/* Pointer into buffers (for reading) */ 
	ubyte *wptr;		/* Pointer into buffers (for writing) */ 

	DLPCMD_TRACE(1, ">>> ReadDBList flags 0x%02x, card %d, start %d\n",
		     flags, card, start);

	header.id = DLPCMD_ReadDBList;
	header.argc = 1;

	/* Construct request header in 'outbuf' */
	wptr = outbuf;
	put_ubyte(&wptr, flags);
	put_ubyte(&wptr, card);
	put_uword(&wptr, start);

	argv[0].id = DLPARG_BASE;
	argv[0].size = 4;
	argv[0].data = outbuf;

	err = dlp_send_req(fd, &header, argv);
if (err < 0) return err;
	err = dlp_recv_resp(fd, DLPCMD_ReadDBList, &resp_header, 1, argv);
if (err < 0) return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Should do something smart, like set dlpcmd_errno
		 * to 'resp_header.errno', and return -1 or something.
		 */
		return -resp_header.errno;

/* Parse the response header */
rptr = argv[0].data;
list_header.lastindex = get_uword(&rptr);
list_header.flags = get_ubyte(&rptr);
list_header.act_count = get_ubyte(&rptr);
fprintf(stderr, "List header: last %d, flags 0x%02x, count %d\n",
	list_header.lastindex,
	list_header.flags,
	list_header.act_count);

/* Parse the info for this database */
dbinfo.size = get_ubyte(&rptr);
dbinfo.misc_flags = get_ubyte(&rptr);
dbinfo.db_flags = get_uword(&rptr);
dbinfo.type = get_udword(&rptr);
dbinfo.creator = get_udword(&rptr);
dbinfo.version = get_uword(&rptr);
dbinfo.modnum = get_udword(&rptr);
dbinfo.ctime.year = get_uword(&rptr);
dbinfo.ctime.month = get_ubyte(&rptr);
dbinfo.ctime.day = get_ubyte(&rptr);
dbinfo.ctime.hour = get_ubyte(&rptr);
dbinfo.ctime.minute = get_ubyte(&rptr);
dbinfo.ctime.second = get_ubyte(&rptr);
get_ubyte(&rptr);
dbinfo.mtime.year = get_uword(&rptr);
dbinfo.mtime.month = get_ubyte(&rptr);
dbinfo.mtime.day = get_ubyte(&rptr);
dbinfo.mtime.hour = get_ubyte(&rptr);
dbinfo.mtime.minute = get_ubyte(&rptr);
dbinfo.mtime.second = get_ubyte(&rptr);
get_ubyte(&rptr);
dbinfo.baktime.year = get_uword(&rptr);
dbinfo.baktime.month = get_ubyte(&rptr);
dbinfo.baktime.day = get_ubyte(&rptr);
dbinfo.baktime.hour = get_ubyte(&rptr);
dbinfo.baktime.minute = get_ubyte(&rptr);
dbinfo.baktime.second = get_ubyte(&rptr);
get_ubyte(&rptr);
dbinfo.index = get_uword(&rptr);

fprintf(stderr, "Database info:\n");
fprintf(stderr, "\tsize %d, misc flags 0x%02x, DB flags 0x%04x\n",
	dbinfo.size,
	dbinfo.misc_flags,
	dbinfo.db_flags);
fprintf(stderr, "\tDB flags:");
if (dbinfo.db_flags & DLPCMD_DBFLAG_RESDB) fprintf(stderr, " RESDB");
if (dbinfo.db_flags & DLPCMD_DBFLAG_RO) fprintf(stderr, " RO");
if (dbinfo.db_flags & DLPCMD_DBFLAG_APPDIRTY) fprintf(stderr, " APPDIRTY");
if (dbinfo.db_flags & DLPCMD_DBFLAG_BACKUP) fprintf(stderr, " BACKUP");
if (dbinfo.db_flags & DLPCMD_DBFLAG_OKNEWER) fprintf(stderr, " OKNEWER");
if (dbinfo.db_flags & DLPCMD_DBFLAG_RESET) fprintf(stderr, " RESET");
if (dbinfo.db_flags & DLPCMD_DBFLAG_OPEN) fprintf(stderr, " OPEN");
fprintf(stderr, "\n");
fprintf(stderr, "\ttype '%c%c%c%c' (0x%08lx), creator '%c%c%c%c' (0x%08lx), version %d, modnum %ld\n",
	(char) (dbinfo.type >> 24) & 0xff,
	(char) (dbinfo.type >> 16) & 0xff,
	(char) (dbinfo.type >> 8) & 0xff,
	(char) dbinfo.type & 0xff,
	dbinfo.type,
	(char) (dbinfo.creator >> 24) & 0xff,
	(char) (dbinfo.creator >> 16) & 0xff,
	(char) (dbinfo.creator >> 8) & 0xff,
	(char) dbinfo.creator & 0xff,
	dbinfo.creator,
	dbinfo.version,
	dbinfo.modnum);
fprintf(stderr, "\tCreated %02d:%02d:%02d, %d/%d/%d\n",
	dbinfo.ctime.hour,
	dbinfo.ctime.minute,
	dbinfo.ctime.second,
	dbinfo.ctime.day,
	dbinfo.ctime.month,
	dbinfo.ctime.year);
fprintf(stderr, "\tModified %02d:%02d:%02d, %d/%d/%d\n",
	dbinfo.mtime.hour,
	dbinfo.mtime.minute,
	dbinfo.mtime.second,
	dbinfo.mtime.day,
	dbinfo.mtime.month,
	dbinfo.mtime.year);
fprintf(stderr, "\tBacked up %02d:%02d:%02d, %d/%d/%d\n",
	dbinfo.baktime.hour,
	dbinfo.baktime.minute,
	dbinfo.baktime.second,
	dbinfo.baktime.day,
	dbinfo.baktime.month,
	dbinfo.baktime.year);
fprintf(stderr, "\tindex %d\n",
	dbinfo.index);
fprintf(stderr, "\tName: \"%-*s\"\n",
	dbinfo.size - 44,
	rptr);

/*  return 0; */
return list_header.flags;
}

/* DlpOpenDB
 * Open a database.
 */
int
DlpOpenDB(int fd,		/* File descriptor */
	  int card,		/* Memory card */
	  const char *name,	/* Database name */
	  ubyte mode,		/* Open mode */
	  ubyte *dbhandle)	/* Handle to open database will be put here */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];			/* Argument list */
	static ubyte outbuf[128];	/* Output buffer */
					/* XXX - Fixed size: bad! */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPCMD_TRACE(1, ">>> OpenDB: card %d, name \"%s\", mode 0x%02x\n",
		     card, name, mode);

	/* Fill in the header values */
	header.id = DLPCMD_OpenDB;
	header.argc = 1;

	/* Construct the argument */
	/* XXX - This could use some work */
	wptr = outbuf;
	put_ubyte(&wptr, card);
	put_ubyte(&wptr, mode);
	memcpy(wptr, name, strlen(name)+1);
	wptr += strlen(name)+1;

	/* Fill in the argument */
	argv[0].id = DLPARG_OpenDB_DB;
	argv[0].size = wptr - outbuf;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(10, "DlpOpenDB: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(fd, DLPCMD_OpenDB, &resp_header, 1, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Should do something smart, like set dlpcmd_errno
		 * to 'resp_header.errno', and return -1 or something.
		 */
		return -resp_header.errno;

	/* XXX - Check number of response arguments */
	/* Parse the response argument */
	rptr = argv[0].data;
	*dbhandle = get_ubyte(&rptr);

	DLPCMD_TRACE(3, "Database handle: %d\n", *dbhandle);

	return 0;		/* Success */
}

/* DlpCreateDB
 * Create a new database according to the specifications given in
 * 'newdb'.
 * Puts a handle for the newly-created database in 'dbhandle'.
 */
int
DlpCreateDB(int fd,		/* File descriptor */
	    const struct dlp_createdbreq *newdb,
				/* Describes the database to create */
	    ubyte *dbhandle)	/* Database handle */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];			/* Argument list */
	static ubyte outbuf[1024];	/* Output buffer */
					/* Fixed size: bad! Find out the
					 * maximum size it can be. */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPCMD_TRACE(1, ">>> CreateDB: creator 0x%08lx, type 0x%08lx, card %d, flags 0x%02x, version %d, name \"%s\"\n",
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
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(10, "DlpCreateDB: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(fd, DLPCMD_CreateDB, &resp_header, 1, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Should do something smart, like set dlpcmd_errno
		 * to 'resp_header.errno', and return -1 or something.
		 */
		return -resp_header.errno;

	/* XXX - Check number of response arguments */
	/* Parse the response argument */
	rptr = argv[0].data;
	*dbhandle = get_ubyte(&rptr);

	DLPCMD_TRACE(3, "Database handle: %d\n", *dbhandle);

	return 0;		/* Success */
}

/* DlpCloseDB
 * Close the database with the handle 'handle'. If 'handle' is
 * DLPCMD_CLOSEALLDBS, then all open databases will be closed.
 */
int
DlpCloseDB(int fd,		/* File descriptor */
	   ubyte handle)	/* Handle of database to delete */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Argument list */

	DLPCMD_TRACE(1, ">>> CloseDB(%d)\n", handle);

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
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(10, "DlpCloseDB: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(fd, DLPCMD_CloseDB, &resp_header, 0, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Should do something smart, like set dlpcmd_errno
		 * to 'resp_header.errno', and return -1 or something.
		 */
		return -resp_header.errno;

	/* XXX - Check number of response arguments */
	return 0;		/* Success */
}

/* DlpDeleteDB
 * Deletes a database. The database must be closed.
 */
int
DlpDeleteDB(int fd,		/* File descriptor */
	    const int card,	/* Memory card */
	    const char *name)	/* Name of the database */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];			/* Argument list */
	static ubyte outbuf[128];	/* Output buffer */
					/* XXX - Fixed size: bad! */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPCMD_TRACE(1, ">>> DeleteDB: card %d, name \"%s\"\n",
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
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(10, "DlpDeleteDB: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(fd, DLPCMD_DeleteDB, &resp_header, 1, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Should do something smart, like set dlpcmd_errno
		 * to 'resp_header.errno', and return -1 or something.
		 */
		return -resp_header.errno;

	/* XXX - Check number of response arguments */

	return 0;		/* Success */
}

/* DlpReadAppBlock
 * Read the AppInfo block for a database.
 */
int
DlpReadAppBlock(int fd,		/* File descriptor */
		struct dlp_appblock *appblock,
				/* Application block descriptor */
		uword *len,	/* # bytes read returned here */
		ubyte *data)	/* Set to the data read */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];			/* Argument list */
	static ubyte outbuf[1024];	/* Output buffer */
					/* XXX - Fixed size: bad */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPCMD_TRACE(1, ">>> ReadAppBlock\n");

	/* Fill in the header values */
	header.id = DLPCMD_ReadAppBlock;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, appblock->dbid);
	put_ubyte(&wptr, 0);		/* Unused */
	put_uword(&wptr, appblock->offset);
	put_uword(&wptr, appblock->len);

	/* Fill in the argument */
	argv[0].id = DLPARG_ReadAppBlock_Req;
	argv[0].size = DLPARGLEN_ReadAppBlock_Req;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	/* Get a response */
	DLPCMD_TRACE(10, "DlpReadAppBlock: waiting for response\n");
	err = dlp_recv_resp(fd, DLPCMD_ReadAppBlock, &resp_header, 1, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);

	/* XXX - Make sure the number of arguments is sane, and that
	 * they're of known types.
	 */
	/* XXX - Sanity check: Make sure argv[0].size <= len */
	/* Return the data and its length to the caller */
/*  	*len = argv[0].size; */
rptr = argv[0].data;
*len = get_uword(&rptr);
	memcpy(data, rptr, *len);
	rptr += *len;

	DLPCMD_TRACE(3, "block size: %d (0x%04x)\n", *len, *len);
	debug_dump(stderr, "APP: ", data, *len);

	return 0;		/* Success */
}

/* DlpWriteAppBlock
 * XXX - The API probably needs to be reworked.
 */
int
DlpWriteAppBlock(int fd,	/* File descriptor */
		 const struct dlp_appblock *appblock,
				/* Application block descriptor */
		 ubyte *data)	/* The data to write */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];			/* Argument list */
	static ubyte outbuf[1024];	/* Output buffer */
					/* XXX - Fixed size: bad */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPCMD_TRACE(1, ">>> WriteAppBlock\n");

	/* Fill in the header values */
	header.id = DLPCMD_WriteAppBlock;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, appblock->dbid);
	put_ubyte(&wptr, 0);		/* Unused */
	put_uword(&wptr, appblock->len);

	/* Fill in the argument */
	argv[0].id = DLPARG_WriteAppBlock_Block;
	argv[0].size = DLPARGLEN_WriteAppBlock_Block + appblock->len;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	/* Get a response */
	DLPCMD_TRACE(10, "DlpWriteAppBlock: waiting for response\n");
	err = dlp_recv_resp(fd, DLPCMD_WriteAppBlock, &resp_header, 1, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);

	/* XXX - Make sure the number of arguments is sane, and that
	 * they're of known types.
	 */
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Probably ought to do something smart */
		return -resp_header.errno;

	return 0;		/* Success */
}

/* DlpReadSortBlock
 * Read the sort block for a database.
 * XXX - I think this is used by the application to sort its records.
 * XXX - Not terribly well-tested.
 */
int
DlpReadSortBlock(int fd,	/* File descriptor */
		struct dlp_sortblock *sortblock,
				/* Sort block descriptor */
		uword *len,	/* # bytes read returned here */
		ubyte *data)	/* Set to the data read */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];			/* Argument list */
	static ubyte outbuf[1024];	/* Output buffer */
					/* XXX - Fixed size: bad */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPCMD_TRACE(1, ">>> ReadSortBlock\n");

	/* Fill in the header values */
	header.id = DLPCMD_ReadSortBlock;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, sortblock->dbid);
	put_ubyte(&wptr, 0);		/* Unused */
	put_uword(&wptr, sortblock->offset);
	put_uword(&wptr, sortblock->len);

	/* Fill in the argument */
	argv[0].id = DLPARG_ReadSortBlock_Req;
	argv[0].size = DLPARGLEN_ReadSortBlock_Req;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	/* Get a response */
	DLPCMD_TRACE(10, "DlpReadSortBlock: waiting for response\n");
	err = dlp_recv_resp(fd, DLPCMD_ReadSortBlock, &resp_header, 1, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);

	/* XXX - Make sure the number of arguments is sane, and that
	 * they're of known types.
	 */
	/* XXX - Sanity check: Make sure argv[0].size <= len */
	/* Return the data and its length to the caller */
	*len = argv[0].size;
	memcpy(data, argv[0].data, argv[0].size);

	return 0;		/* Success */
}

/* DlpWriteSortBlock
 * Write the database's sort block.
 * XXX - The API probably needs to be reworked.
 * XXX - Not terribly well-tested.
 */
int
DlpWriteSortBlock(int fd,	/* File descriptor */
		 const struct dlp_sortblock *sortblock,
				/* Sort block descriptor */
		 ubyte *data)	/* The data to write */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];			/* Argument list */
	static ubyte outbuf[1024];	/* Output buffer */
					/* XXX - Fixed size: bad */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPCMD_TRACE(1, ">>> WriteSortBlock\n");

	/* Fill in the header values */
	header.id = DLPCMD_WriteSortBlock;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, sortblock->dbid);
	put_ubyte(&wptr, 0);		/* Unused */
	put_uword(&wptr, sortblock->len);

	/* Fill in the argument */
	argv[0].id = DLPARG_WriteSortBlock_Block;
	argv[0].size = DLPARGLEN_WriteSortBlock_Block + sortblock->len;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	/* Get a response */
	DLPCMD_TRACE(10, "DlpWriteSortBlock: waiting for response\n");
	err = dlp_recv_resp(fd, DLPCMD_WriteSortBlock, &resp_header, 1, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);

	/* XXX - Make sure the number of arguments is sane, and that
	 * they're of known types.
	 */
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Probably ought to do something smart */
		return -resp_header.errno;

	return 0;		/* Success */
}

/* DlpReadNextModifiedRec

 * Read the next modified record in the open database whose handle is
 * 'handle'.
 * When there are no more modified records to be read, the DLP response
 * code will be DLPSTAT_NOTFOUND.
 * XXX - The caller needs access to this.
 */
int
DlpReadNextModifiedRec(int fd,	/* File descriptor */
		       const ubyte handle,
				/* Database ID (handle) */
		       struct dlp_readrecret *record)
				/* Record will be put here */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];			/* Argument list */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */

	DLPCMD_TRACE(1, ">>> ReadNextModifiedRec: db %d\n",
		     handle);

	/* Fill in the header values */
	header.id = DLPCMD_ReadNextModifiedRec;
	header.argc = 1;

	/* Fill in the argument */
	argv[0].id = DLPARG_ReadNextModifiedRec_Req;
	argv[0].size = DLPARGLEN_ReadNextModifiedRec_Req;
	argv[0].data = (void *) &handle/*outbuf*/;

	/* Send the DLP request */
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(10, "DlpReadNextModifiedRec: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(fd, DLPCMD_ReadNextModifiedRec, &resp_header, 1, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Should do something smart, like set dlpcmd_errno
		 * to 'resp_header.errno', and return -1 or something.
		 */
		return -resp_header.errno;

	/* XXX - Check number of response arguments */
	/* Parse the response argument */
	rptr = argv[0].data;
	record->recid = get_udword(&rptr);
	record->index = get_uword(&rptr);
	record->size = get_uword(&rptr);
	record->attributes = get_ubyte(&rptr);
	record->category = get_ubyte(&rptr);
	record->data = rptr;

	DLPCMD_TRACE(6, "Read a record (by ID):\n");
	DLPCMD_TRACE(6, "\tID == 0x%08lx\n", record->recid);
	DLPCMD_TRACE(6, "\tindex == 0x%04x\n", record->index);
	DLPCMD_TRACE(6, "\tsize == 0x%04x\n", record->size);
	DLPCMD_TRACE(6, "\tattributes == 0x%02x\n", record->attributes);
	DLPCMD_TRACE(6, "\tcategory == 0x%02x\n", record->category);

	return 0;		/* Success */
}

int
DlpReadRecordByID(int fd,	/* File descriptor */
		  const struct dlp_readrecreq_byid *req,
				/* Read request arguments */
		  struct dlp_readrecret *record)
				/* Record will be put here */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];			/* Argument list */
	static ubyte outbuf[1024];	/* Output buffer */
					/* XXX - Fixed size: bad! */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPCMD_TRACE(1, ">>> ReadRecord ByID: dbid %d, recid %ld, offset %d, len %d\n",
		     req->dbid,
		     req->recid,
		     req->offset,
		     req->len);

	/* Fill in the header values */
	header.id = DLPCMD_ReadRecord;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, req->dbid);
	put_ubyte(&wptr, 0);		/* Padding */
	put_udword(&wptr, req->recid);
	put_uword(&wptr, req->offset);
	put_uword(&wptr, req->len);

	/* Fill in the argument */
	argv[0].id = DLPARG_ReadRecord_ByID;
	argv[0].size = DLPARGLEN_ReadRecord_ByID;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(10, "DlpReadRecordByID: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(fd, DLPCMD_ReadRecord, &resp_header, 1, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Should do something smart, like set dlpcmd_errno
		 * to 'resp_header.errno', and return -1 or something.
		 */
		return -resp_header.errno;

	/* XXX - Check number of response arguments */
	/* Parse the response argument */
	rptr = argv[0].data;
	record->recid = get_udword(&rptr);
	record->index = get_uword(&rptr);
	record->size = get_uword(&rptr);
	record->attributes = get_ubyte(&rptr);
	record->category = get_ubyte(&rptr);
	record->data = rptr;

	DLPCMD_TRACE(6, "Read a record (by ID):\n");
	DLPCMD_TRACE(6, "\tID == 0x%08lx\n", record->recid);
	DLPCMD_TRACE(6, "\tindex == 0x%04x\n", record->index);
	DLPCMD_TRACE(6, "\tsize == 0x%04x\n", record->size);
	DLPCMD_TRACE(6, "\tattributes == 0x%02x\n", record->attributes);
	DLPCMD_TRACE(6, "\tcategory == 0x%02x\n", record->category);

	return 0;		/* Success */
}

/* XXX - Contrary to what one might believe, this appears to only return
 * the response header (not the data, as DlpReadRecordByID does).
 * Presumably, PalmOS really, really likes dealing with record IDs, and not
 * indices. Hence, you're supposed to use this function to get the ID of
 * the record you want to read, but call DlpReadRecordByID to actually read
 * it.
 */
int
DlpReadRecordByIndex(int fd,
		     const struct dlp_readrecreq_byindex *req,
		     struct dlp_readrecret *record)
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];			/* Argument list */
	static ubyte outbuf[1024];	/* Output buffer */
					/* XXX - Fixed size: bad! */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPCMD_TRACE(1, ">>> ReadRecord ByIndex: dbid %d, index %d, offset %d, len %d\n",
		     req->dbid,
		     req->index,
		     req->offset,
		     req->len);

	/* Fill in the header values */
	header.id = DLPCMD_ReadRecord;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, req->dbid);
	put_ubyte(&wptr, 0);		/* Padding */
	put_udword(&wptr, req->index);
	put_uword(&wptr, req->offset);
	put_uword(&wptr, req->len);

	/* Fill in the argument */
	argv[0].id = DLPARG_ReadRecord_ByIndex;
	argv[0].size = DLPARGLEN_ReadRecord_ByIndex;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(10, "DlpReadRecordByIndex: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(fd, DLPCMD_ReadRecord, &resp_header, 1, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Should do something smart, like set dlpcmd_errno
		 * to 'resp_header.errno', and return -1 or something.
		 */
		return -resp_header.errno;

	/* XXX - Check number of response arguments */
	/* Parse the response argument */
	rptr = argv[0].data;
	record->recid = get_udword(&rptr);
	record->index = get_uword(&rptr);
	record->size = get_uword(&rptr);
	record->attributes = get_ubyte(&rptr);
	record->category = get_ubyte(&rptr);
	record->data = rptr;

	DLPCMD_TRACE(6, "Read a record (by ID):\n");
	DLPCMD_TRACE(6, "\tID == 0x%08lx\n", record->recid);
	DLPCMD_TRACE(6, "\tindex == 0x%04x\n", record->index);
	DLPCMD_TRACE(6, "\tsize == 0x%04x\n", record->size);
	DLPCMD_TRACE(6, "\tattributes == 0x%02x\n", record->attributes);
	DLPCMD_TRACE(6, "\tcategory == 0x%02x\n", record->category);

	return 0;		/* Success */
}

/* DlpWriteRecord
 * Write a record.
 * XXX - The Palm seems rather picky about this: apparently, you can't just
 * use any old string as the data, or else DlpWriteRecord() will return
 * "invalid parameter". I've been able to successfully write a record read
 * from another database, but not create a new one myself.
 */
int
DlpWriteRecord(int fd,		/* File descriptor */
	       const udword len,	/* Length of record data */
	       const struct dlp_writerec *rec,
				/* Record descriptor */
	       udword *recid)	/* Record ID returned here */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];			/* Argument list */
	static ubyte outbuf[2048];	/* Output buffer */
					/* XXX - Fixed size: bad! */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPCMD_TRACE(1, ">>> WriteRecord: dbid %d, flags 0x%02x, recid 0x%08lx, attr 0x%02x, category %d, len %ld\n",
		     rec->dbid,
		     rec->flags,
		     rec->recid,
		     rec->attributes,
		     rec->category,
		     len);

	/* Fill in the header values */
	header.id = DLPCMD_WriteRecord;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, rec->dbid);
	put_ubyte(&wptr, rec->flags);
	put_udword(&wptr, rec->recid);
	put_ubyte(&wptr, rec->attributes);
	/* XXX - Potential buffer overflow. Check this */
	memcpy(wptr, rec->data, len);
	wptr += len;

	/* Fill in the argument */
	argv[0].id = DLPARG_WriteRecord_Rec;
	argv[0].size = wptr - outbuf;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(10, "DlpWriteRecord: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(fd, DLPCMD_WriteRecord, &resp_header, 1, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Should do something smart, like set dlpcmd_errno
		 * to 'resp_header.errno', and return -1 or something.
		 */
		return -resp_header.errno;

	/* XXX - Check number of response arguments */
	/* Parse the response argument */
	rptr = argv[0].data;
	*recid = get_udword(&rptr);

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
DlpDeleteRecord(int fd,			/* File descriptor */
		const ubyte dbid,	/* Database ID (handle) */
		const ubyte flags,	/* Flags */
		const udword recid)	/* Unique record ID */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];			/* Argument list */
	static ubyte outbuf[DLPARGLEN_DeleteRecord_Rec];
						/* Output buffer */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPCMD_TRACE(1, ">>> DeleteRecord: dbid %d, flags 0x%02x, recid 0x%08lx\n",
		     dbid, flags, recid);

	/* Fill in the header values */
	header.id = DLPCMD_DeleteRecord;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, dbid);
	put_ubyte(&wptr, flags);
	put_udword(&wptr, recid);

	/* Fill in the argument */
	argv[0].id = DLPARG_DeleteRecord_Rec;
	argv[0].size = DLPARGLEN_DeleteRecord_Rec;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(10, "DlpDeleteRecord: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(fd, DLPCMD_DeleteRecord, &resp_header, 1, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Should do something smart, like set dlpcmd_errno
		 * to 'resp_header.errno', and return -1 or something.
		 */
		return -resp_header.errno;

	/* XXX - Check number of response arguments */

	return 0;		/* Success */
}

int
DlpReadResourceByIndex(int fd,		/* File descriptor */
		       const ubyte dbid,	/* Database ID (handle) */
		       const uword index,	/* Resource index */
		       const uword offset,	/* Offset into resource */
		       const uword len,		/* #bytes to read (-1 == to
						 * the end) */
		       struct dlp_resource *value,
						/* Resource info returned
						 * here */
		       ubyte *data)		/* Resource data returned
						 * here */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];			/* Argument list */
	static ubyte outbuf[2048];	/* Output buffer */
					/* XXX - Fixed size: bad! */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPCMD_TRACE(1, ">>> ReadResourceByIndex: dbid %d, index %d, offset %d, len %d\n",
		     dbid, index, offset, len);

	/* Fill in the header values */
	header.id = DLPCMD_ReadResource;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, dbid);
	put_ubyte(&wptr, 0);		/* Padding */
	put_uword(&wptr, index);
	put_uword(&wptr, offset);
	put_uword(&wptr, len);

	/* Fill in the argument */
	argv[0].id = DLPARG_ReadResource_ByIndex;
	argv[0].size = DLPARGLEN_ReadResource_ByIndex;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(10, "DlpReadResourceByIndex: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(fd, DLPCMD_ReadResource, &resp_header, 1, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Should do something smart, like set dlpcmd_errno
		 * to 'resp_header.errno', and return -1 or something.
		 */
		return -resp_header.errno;

	/* XXX - Check number of response arguments */
	/* Parse the response argument */
	rptr = argv[0].data;
	value->type = get_udword(&rptr);
	value->id = get_uword(&rptr);
	value->index = get_uword(&rptr);
	value->size = get_uword(&rptr);
	/* XXX - Potential buffer overflow */
	memcpy(data, rptr, value->size);
	rptr += value->size;

	DLPCMD_TRACE(3, "Resource: type '%c%c%c%c' (0x%08lx), id %d, index %d, size %d\n",
		     (char) (value->type >> 24) & 0xff,
		     (char) (value->type >> 16) & 0xff,
		     (char) (value->type >> 8) & 0xff,
		     (char) value->type & 0xff,
		     value->type,
		     value->id,
		     value->index,
		     value->size);

	return 0;		/* Success */
}

int
DlpReadResourceByType(int fd,			/* File descriptor */
		      const ubyte dbid,		/* Database ID (handle) */
		      const udword type,	/* Resource type */
		      const uword id,		/* Resource ID */
		      const uword offset,	/* Offset into resource */
		      const uword len,		/* # bytes to read */
		      struct dlp_resource *value,
						/* Resource info returned
						 * here */
		      ubyte *data)		/* Resource data returned
						 * here */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];			/* Argument list */
	static ubyte outbuf[2048];	/* Output buffer */
					/* XXX - Fixed size: bad! */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPCMD_TRACE(1, ">>> ReadResourceByType: dbid %d, type %ld, id %d, offset %d, len %d\n",
		     dbid, type, id, offset, len);

	/* Fill in the header values */
	header.id = DLPCMD_ReadResource;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, dbid);
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
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(10, "DlpReadResourceByType: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(fd, DLPCMD_ReadResource, &resp_header, 1, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Should do something smart, like set dlpcmd_errno
		 * to 'resp_header.errno', and return -1 or something.
		 */
		return -resp_header.errno;

	/* XXX - Check number of response arguments */
	/* Parse the response argument */
	rptr = argv[0].data;
	value->type = get_udword(&rptr);
	value->id = get_uword(&rptr);
	value->index = get_uword(&rptr);
	value->size = get_uword(&rptr);
	/* XXX - Potential buffer overflow */
	memcpy(data, rptr, value->size);
	rptr += value->size;

	DLPCMD_TRACE(3, "Resource: type '%c%c%c%c' (0x%08lx), id %d, index %d, size %d\n",
		     (char) (value->type >> 24) & 0xff,
		     (char) (value->type >> 16) & 0xff,
		     (char) (value->type >> 8) & 0xff,
		     (char) value->type & 0xff,
		     value->type,
		     value->id,
		     value->index,
		     value->size);

	return 0;		/* Success */
}

int
DlpWriteResource(int fd,		/* File descriptor */
		 const ubyte dbid,	/* Database ID (handle) */
		 const udword type,	/* Resource type */
		 const uword id,	/* Resource ID */
		 const uword size,	/* Size of resource */
		 const ubyte *data)	/* Resource data */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];			/* Argument list */
	static ubyte outbuf[2048];	/* Output buffer */
					/* XXX - Fixed size: bad! */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPCMD_TRACE(1, "WriteResource: type '%c%c%c%c' (0x%08lx), id %d, size %d\n",
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
	wptr = outbuf;
	put_ubyte(&wptr, dbid);
	put_ubyte(&wptr, 0);		/* Padding */
	put_udword(&wptr, type);
	put_uword(&wptr, id);
	put_uword(&wptr, size);
	/* XXX - Potential buffer overflow */
	memcpy(wptr, data, size);
	wptr += size;

	/* Fill in the argument */
	argv[0].id = DLPARG_WriteResource_Res;
	argv[0].size = wptr - outbuf;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(10, "DlpWriteResource: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(fd, DLPCMD_WriteResource, &resp_header, 1, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Should do something smart, like set dlpcmd_errno
		 * to 'resp_header.errno', and return -1 or something.
		 */
		return -resp_header.errno;

	/* XXX - Check number of response arguments */

	return 0;		/* Success */
}

int
DlpDeleteResource(int fd,		/* File descriptor */
		  const ubyte dbid,	/* Database ID (handle) */
		  const ubyte flags,	/* Request flags */
		  const udword type,	/* Resource type */
		  const uword id)	/* Resource ID */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];			/* Argument list */
	static ubyte outbuf[DLPARGLEN_DeleteResource_Res];
					/* Output buffer */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPCMD_TRACE(1, ">>> DeleteResource: dbid %d, flags 0x%02x, type '%c%c%c%c' (0x%08lx), id %d\n",
		     dbid, flags,
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
	put_ubyte(&wptr, dbid);
	put_ubyte(&wptr, flags);
	put_udword(&wptr, type);
	put_uword(&wptr, id);

	/* Fill in the argument */
	argv[0].id = DLPARG_DeleteResource_Res;
	argv[0].size = DLPARGLEN_DeleteResource_Res;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(10, "DlpDeleteResource: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(fd, DLPCMD_DeleteResource, &resp_header, 1, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Should do something smart, like set dlpcmd_errno
		 * to 'resp_header.errno', and return -1 or something.
		 */
		return -resp_header.errno;

	/* XXX - Check number of response arguments */

	return 0;		/* Success */
}

/* DlpCleanUpDatabase
 * Delete any records that were marked as archived or deleted in the record
 * database.
 */
int DlpCleanUpDatabase(int fd,		/* File descriptor */
		       const ubyte dbid)
					/* Database ID (handle) */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];			/* Argument list */

	DLPCMD_TRACE(1, ">>> CleanUpDatabase: dbid %d\n", dbid);

	/* Fill in the header values */
	header.id = DLPCMD_CleanUpDatabase;
	header.argc = 1;

	/* Fill in the argument */
	argv[0].id = DLPARG_CleanUpDatabase_DB;
	argv[0].size = DLPARGLEN_CleanUpDatabase_DB;
	argv[0].data = (void *) &dbid;

	/* Send the DLP request */
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(10, "DlpCleanUpDatabase: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(fd, DLPCMD_CleanUpDatabase, &resp_header, 0, NULL);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Should do something smart, like set dlpcmd_errno
		 * to 'resp_header.errno', and return -1 or something.
		 */
		return -resp_header.errno;

	/* XXX - Check number of response arguments */

	return 0;		/* Success */
}

/* DlpResetSyncFlags
 * Reset any dirty flags on the database.
 */
int DlpResetSyncFlags(int fd,		/* File descriptor */
		      const ubyte dbid)
					/* Database ID (handle) */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];			/* Argument list */

	DLPCMD_TRACE(1, ">>> ResetSyncFlags: dbid %d\n", dbid);

	/* Fill in the header values */
	header.id = DLPCMD_ResetSyncFlags;
	header.argc = 1;

	/* Fill in the argument */
	argv[0].id = DLPARG_ResetSyncFlags_DB;
	argv[0].size = DLPARGLEN_ResetSyncFlags_DB;
	argv[0].data = (void *) &dbid;

	/* Send the DLP request */
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(10, "DlpResetSyncFlags: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(fd, DLPCMD_ResetSyncFlags, &resp_header, 0, NULL);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Should do something smart, like set dlpcmd_errno
		 * to 'resp_header.errno', and return -1 or something.
		 */
		return -resp_header.errno;

	/* XXX - Check number of response arguments */

	return 0;		/* Success */
}

int
DlpCallApplication(int fd,		/* File descriptor */
		   const udword version,
					/* OS version, used for determining
					 * how to phrase the DLP call. */
		   const struct dlp_appcall *appcall,
					/* Which application to call */
		   const udword paramsize,
					/* Size of the parameter */
		   const ubyte *param,	/* The parameter data */
		   struct dlp_appresult *result)
					/* Application result */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];			/* Argument list */
	static ubyte outbuf[2048];	/* Output buffer */
					/* XXX - Fixed size: bad! */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPCMD_TRACE(1, ">>> CallApplication: ver 0x%08lx, creator '%c%c%c%c' (0x%08lx), action %d, type '%c%c%c%c' (0x%08lx), paramsize %ld\n",
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
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(10, "DlpCallApplication: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(fd, DLPCMD_CallApplication, &resp_header, 1, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Should do something smart, like set dlpcmd_errno
		 * to 'resp_header.errno', and return -1 or something.
		 */
		return -resp_header.errno;

	/* XXX - Check number of response arguments */
	/* Parse the response argument */
	rptr = argv[0].data;
	switch (resp_header.id)
	{
	    case DLPRET_CallApplication_V1:
		result->action = get_uword(&rptr);
		result->result = get_uword(&rptr);
		result->size = get_uword(&rptr);
		break;
	    case DLPRET_CallApplication_V2:
		result->result = get_udword(&rptr);
		result->size = get_udword(&rptr);
		/* The reserved fields aren't useful, but they might
		 * conceivably be interesting.
		 */
		result->reserved1 = get_udword(&rptr);
		result->reserved2 = get_udword(&rptr);
	    default:
		/* Invalid result ID */
		palm_errno = PALMERR_BADRESID;
		return -1;
	}
	/* XXX - Potential buffer overflow */
	memcpy(result->data, rptr, result->size);

	return 0;		/* Success */
}

/* DlpResetSystem
 * Indicate that the Palm needs to be reset at the end of the sync.
 */
int
DlpResetSystem(int fd)		/* File descriptor */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */

	DLPCMD_TRACE(1, ">>> ResetSystem\n");

	/* Fill in the header values */
	header.id = DLPCMD_ResetSystem;
	header.argc = 0;

	/* Send the DLP request */
	err = dlp_send_req(fd, &header, NULL);
	if (err < 0)
		return err;

	DLPCMD_TRACE(10, "DlpResetSystem: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(fd, DLPCMD_ResetSystem, &resp_header, 0, NULL);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Should do something smart, like set dlpcmd_errno
		 * to 'resp_header.errno', and return -1 or something.
		 */
		return -resp_header.errno;

	/* XXX - Check number of response arguments */

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
DlpAddSyncLogEntry(int fd,	/* File descriptor */
		   const char *msg)	/* Log message */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];			/* Argument list */

	DLPCMD_TRACE(1, ">>> AddSyncLogEntry \"%s\"\n", msg);

	/* Fill in the header values */
	header.id = DLPCMD_AddSyncLogEntry;
	header.argc = 1;

	/* Fill in the argument */
	argv[0].id = DLPARG_AddSyncLogEntry_Msg;
	argv[0].size = strlen(msg)+1;	/* Include the final NUL */
	argv[0].data = (ubyte *) msg;

	/* Send the DLP request */
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(10, "DlpAddSyncLogEntry: waiting for response\n");
	/* Get a response */
	err = dlp_recv_resp(fd, DLPCMD_AddSyncLogEntry, &resp_header, 0, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);

	return 0;		/* Success */
}

/* DlplReadOpenDBInfo
 * Read information about an open database.
 */
int
DlpReadOpenDBInfo(int fd,	/* File descriptor */
		  ubyte handle,	/* Database handle */
		  struct dlp_opendbinfo *dbinfo)
				/* Info about database */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];			/* Argument list */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */

	DLPCMD_TRACE(1, ">>> ReadOpenDBInfo(%d)\n", handle);

	/* Fill in the header values */
	header.id = DLPCMD_ReadOpenDBInfo;
	header.argc = 1;

	/* Fill in the argument */
	argv[0].id = DLPARG_ReadOpenDBInfo_DB;
	argv[0].size = DLPARGLEN_ReadOpenDBInfo_DB;
	argv[0].data = &handle;

	/* Send the DLP request */
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	/* Get a response */
	DLPCMD_TRACE(10, "DlpReadOpenDBInfo: waiting for response\n");
	err = dlp_recv_resp(fd, DLPCMD_ReadOpenDBInfo, &resp_header, 1, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);

	/* XXX - Make sure the number of arguments is sane, and that
	 * they're of known types.
	 */
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Probably ought to do something smart */
		return -resp_header.errno;

	/* Extract the results from the argument */
	rptr = argv[0].data;
	dbinfo->numrecs = get_uword(&rptr);

	return 0;
}

int
DlpMoveCategory(int fd,			/* File descriptor */
		const ubyte handle,	/* Database ID (handle) */
		const ubyte from,	/* ID of source category */
		const ubyte to)		/* ID of destination category */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];			/* Argument list */
	static ubyte outbuf[DLPARGLEN_MoveCategory_Cat];
					/* Output buffer */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPCMD_TRACE(1, ">>> MoveCategory: handle %d, from %d, to %d\n",
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
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(10, "DlpMoveCategory: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(fd, DLPCMD_MoveCategory, &resp_header, 1, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Should do something smart, like set dlpcmd_errno
		 * to 'resp_header.errno', and return -1 or something.
		 */
		return -resp_header.errno;

	/* XXX - Check number of response arguments */

	return 0;		/* Success */
}

/* DlpOpenConduit

 * Sent before each conduit is opened by the desktop.
 */
int DlpOpenConduit(int fd)		/* File descriptor */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */

	DLPCMD_TRACE(1, ">>> OpenConduit:\n");

	/* Fill in the header values */
	header.id = DLPCMD_OpenConduit;
	header.argc = 0;

	/* Send the DLP request */
	err = dlp_send_req(fd, &header, NULL);
	if (err < 0)
		return err;

	DLPCMD_TRACE(10, "DlpOpenConduit: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(fd, DLPCMD_OpenConduit, &resp_header, 0, NULL);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Should do something smart, like set dlpcmd_errno
		 * to 'resp_header.errno', and return -1 or something.
		 */
		return -resp_header.errno;

	/* XXX - Check number of response arguments */

	return 0;		/* Success */
}

int
DlpEndOfSync(int fd,		/* File descriptor */
	     const ubyte status)
				/* Exit status, reason for termination */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];			/* Argument list */
	static ubyte status_buf[2];	/* Buffer for the status word */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPCMD_TRACE(1, ">>> EndOfSync status %d\n", status);

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
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	/* Get a response */
	DLPCMD_TRACE(10, "DlpEndOfSync: waiting for response\n");
	err = dlp_recv_resp(fd, DLPCMD_EndOfSync, &resp_header, 0, NULL);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);

	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Should do something smart, like set dlpcmd_errno
		 * to 'resp_header.errno', and return -1 or something.
		 */
		return -resp_header.errno;

	/* XXX - Check number of response arguments */

	return 0;		/* Success */
}

/* DlpResetRecordIndex
 * Reset the "next modified record" index to the beginning.
 */
int DlpResetRecordIndex(int fd,		/* File descriptor */
			const ubyte dbid)
					/* Database ID (handle) */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];			/* Argument list */

	DLPCMD_TRACE(1, ">>> ResetRecordIndex: dbid %d\n", dbid);

	/* Fill in the header values */
	header.id = DLPCMD_ResetRecordIndex;
	header.argc = 1;

	/* Fill in the argument */
	argv[0].id = DLPARG_ResetRecordIndex_DB;
	argv[0].size = DLPARGLEN_ResetRecordIndex_DB;
	argv[0].data = (void *) &dbid;

	/* Send the DLP request */
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(10, "DlpResetRecordIndex: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(fd, DLPCMD_ResetRecordIndex, &resp_header, 0, NULL);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Should do something smart, like set dlpcmd_errno
		 * to 'resp_header.errno', and return -1 or something.
		 */
		return -resp_header.errno;

	/* XXX - Check number of response arguments */

	return 0;		/* Success */
}

/* DlpReadRecordIDList
 * Read the list of record IDs in the database whose handle is
 * 'handle'.
 */
int
DlpReadRecordIDList(int fd,		/* File descriptor */
		    const struct dlp_idlistreq *idreq,
		    uword *numread,	/* How many entries were read */
		    udword recids[])	/* IDs are returned here */
{
	int i;
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];			/* Argument list */
	static ubyte outbuf[DLPARGLEN_ReadRecordIDList_Req];
					/* Output buffer */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPCMD_TRACE(1, ">>> ReadRecordIDList: handle %d, flags 0x%02x, start %d, max %d\n",
		     idreq->dbid,
		     idreq->flags,
		     idreq->start,
		     idreq->max);

	/* Fill in the header values */
	header.id = DLPCMD_ReadRecordIDList;
	header.argc = 1;

	/* Construct the argument */
	wptr = outbuf;
	put_ubyte(&wptr, idreq->dbid);
	put_ubyte(&wptr, idreq->flags);
	put_uword(&wptr, idreq->start);
	put_uword(&wptr, idreq->max);

	/* Fill in the argument */
	argv[0].id = DLPARG_ReadRecordIDList_Req;
	argv[0].size = DLPARGLEN_ReadRecordIDList_Req;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(10, "DlpReadRecordIDList: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(fd, DLPCMD_ReadRecordIDList, &resp_header, 1, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Should do something smart, like set dlpcmd_errno
		 * to 'resp_header.errno', and return -1 or something.
		 */
		return -resp_header.errno;

	/* XXX - Check number of response arguments */
	/* Parse the response argument */

	rptr = argv[0].data;
	*numread = get_uword(&rptr);
	/* XXX - Make sure idreq->max >= *numread */
	for (i = 0; i < *numread; i++)
		recids[i] = get_udword(&rptr);

	return 0;		/* Success */
}

/* DlpReadNextRecInCategory
 * Read the next record in the given category. The record is returned in
 * 'record'.
 * XXX - Check to make sure the Palm understands v1.1 of the protocol.
 */
int
DlpReadNextRecInCategory(
	int fd,				/* File descriptor */
	const ubyte handle,		/* Database ID (handle) */
	const ubyte category,		/* Category ID */
	struct dlp_readrecret *record)	/* The record will be returned here */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];			/* Argument list */
	static ubyte outbuf[DLPARGLEN_ReadNextRecInCategory_Rec];
					/* Output buffer */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPCMD_TRACE(1, ">>> ReadNextRecInCategory: handle %d, category %d\n",
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
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(10, "DlpReadNextRecInCategory: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(fd, DLPCMD_ReadNextRecInCategory, &resp_header, 1, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Should do something smart, like set dlpcmd_errno
		 * to 'resp_header.errno', and return -1 or something.
		 */
		return -resp_header.errno;

	/* XXX - Check number of response arguments */
	/* Parse the response argument */
	rptr = argv[0].data;

	record->recid = get_udword(&rptr);
	record->index = get_uword(&rptr);
	record->size = get_uword(&rptr);
	record->attributes = get_ubyte(&rptr);
	record->category = get_ubyte(&rptr);
	record->data = rptr;

	DLPCMD_TRACE(6, "Read record in category %d:\n", category);
	DLPCMD_TRACE(6, "\tID == 0x%08lx\n", record->recid);
	DLPCMD_TRACE(6, "\tindex == 0x%04x\n", record->index);
	DLPCMD_TRACE(6, "\tsize == 0x%04x\n", record->size);
	DLPCMD_TRACE(6, "\tattributes == 0x%02x\n", record->attributes);
	DLPCMD_TRACE(6, "\tcategory == 0x%02x\n", record->category);

	return 0;		/* Success */
}

/* DlpReadNextModifiedRecInCategory
 * Read the next modified record in the given category. The record is
 * returned in 'record'.
 * XXX - Check to make sure the Palm understands v1.1 of the protocol.
 */
int
DlpReadNextModifiedRecInCategory(
	int fd,				/* File descriptor */
	const ubyte handle,		/* Database ID (handle) */
	const ubyte category,		/* Category ID */
	struct dlp_readrecret *record)	/* The record will be returned here */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];			/* Argument list */
	static ubyte outbuf[DLPARGLEN_ReadNextModifiedRecInCategory_Rec];
					/* Output buffer */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPCMD_TRACE(1, ">>> ReadNextModifiedRecInCategory: handle %d, category %d\n",
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
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(10, "DlpReadNextModifiedRecInCategory: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(fd, DLPCMD_ReadNextModifiedRecInCategory, &resp_header, 1, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Should do something smart, like set dlpcmd_errno
		 * to 'resp_header.errno', and return -1 or something.
		 */
		return -resp_header.errno;

	/* XXX - Check number of response arguments */
	/* Parse the response argument */
	rptr = argv[0].data;

	record->recid = get_udword(&rptr);
	record->index = get_uword(&rptr);
	record->size = get_uword(&rptr);
	record->attributes = get_ubyte(&rptr);
	record->category = get_ubyte(&rptr);
	record->data = rptr;

	DLPCMD_TRACE(6, "Read record in category %d:\n", category);
	DLPCMD_TRACE(6, "\tID == 0x%08lx\n", record->recid);
	DLPCMD_TRACE(6, "\tindex == 0x%04x\n", record->index);
	DLPCMD_TRACE(6, "\tsize == 0x%04x\n", record->size);
	DLPCMD_TRACE(6, "\tattributes == 0x%02x\n", record->attributes);
	DLPCMD_TRACE(6, "\tcategory == 0x%02x\n", record->category);

	return 0;		/* Success */
}

/* XXX - This is untested: I don't know what values to feed it */
int
DlpReadAppPreference(int fd,		/* File descriptor */
		     const udword creator,
					/* Application creator */
		     const uword id,	/* Preference ID */
		     const uword len,	/* Max # bytes to return */
		     const ubyte flags,
		     struct dlp_apppref *pref,
		     ubyte *data)
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];			/* Argument list */
	static ubyte outbuf[DLPARGLEN_ReadAppPreference_Pref];
					/* Output buffer */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPCMD_TRACE(1, ">>> ReadAppPreference: creator '%c%c%c%c' (0x%08lx), id %d, len %d, flags 0x%02x\n",
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
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(10, "DlpReadAppPreference: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(fd, DLPCMD_ReadAppPreference, &resp_header, 1, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Should do something smart, like set dlpcmd_errno
		 * to 'resp_header.errno', and return -1 or something.
		 */
		return -resp_header.errno;

	/* XXX - Check number of response arguments */
	/* Parse the response argument */
	rptr = argv[0].data;
	pref->version = get_uword(&rptr);
	pref->size = get_uword(&rptr);
	pref->len = get_uword(&rptr);
	/* XXX - Potential buffer overflow */
	memcpy(data, rptr, pref->len);
	rptr += pref->len;

	DLPCMD_TRACE(3, "Read an app. preference: version %d, size %d, len %d\n",
		     pref->version, pref->size, pref->len);

	return 0;		/* Success */
}

/* DlpWriteAppPreference
 * XXX - Test this.
 */
int
DlpWriteAppPreference(int fd,		/* File descriptor */
		      const udword creator,
					/* Application creator */
		      const uword id,	/* Preference ID */
		      const ubyte flags,/* Flags */
		      const struct dlp_apppref *pref,
					/* Preference descriptor */
		      const ubyte *data)/* Preference data */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];			/* Argument list */
	static ubyte outbuf[2048];	/* Output buffer */
					/* XXX - Fixed size: bad! */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPCMD_TRACE(1, ">>> WriteAppPreference: XXX\n");

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
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(10, "DlpWriteRecord: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(fd, DLPCMD_WriteRecord, &resp_header, 1, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Should do something smart, like set dlpcmd_errno
		 * to 'resp_header.errno', and return -1 or something.
		 */
		return -resp_header.errno;

	/* XXX - Check number of response arguments */

	return 0;		/* Success */
}

/* DlpReadNetSyncInfo
 * Read the NetSync information about this Palm: the name and address of
 * the host it net-syncs with.
 * XXX - Check to make sure the Palm understands v1.1 of the protocol.
 */
int
DlpReadNetSyncInfo(int fd,
		   struct dlp_netsyncinfo *netsyncinfo)
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];			/* Argument list */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */

	DLPCMD_TRACE(1, ">>> ReadNetSyncInfo\n");

	/* Fill in the header values */
	header.id = DLPCMD_ReadNetSyncInfo;
	header.argc = 0;

	/* Send the DLP request */
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(10, "DlpReadNetSyncInfo: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(fd, DLPCMD_ReadNetSyncInfo, &resp_header, 1, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Should do something smart, like set dlpcmd_errno
		 * to 'resp_header.errno', and return -1 or something.
		 */
		return -resp_header.errno;

	/* XXX - Check number of response arguments */
	/* Parse the response argument */
	rptr = argv[0].data;
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
	memcpy(netsyncinfo->synchostname, rptr, netsyncinfo->hostnamesize);
	rptr += netsyncinfo->hostnamesize;
	memcpy(netsyncinfo->synchostaddr, rptr, netsyncinfo->hostaddrsize);
	rptr += netsyncinfo->hostaddrsize;
	memcpy(netsyncinfo->synchostnetmask, rptr,
	       netsyncinfo->hostnetmasksize);
	rptr += netsyncinfo->hostnetmasksize;

	DLPCMD_TRACE(6, "NetSync info:\n");
	DLPCMD_TRACE(6, "\tLAN sync: %d\n", netsyncinfo->lansync_on);
	DLPCMD_TRACE(6, "\thostname: (%d) \"%s\"\n",
		     netsyncinfo->hostnamesize,
		     netsyncinfo->synchostname);
	DLPCMD_TRACE(6, "\taddress: (%d) \"%s\"\n",
		     netsyncinfo->hostaddrsize,
		     netsyncinfo->synchostaddr);
	DLPCMD_TRACE(6, "\tnetmask: (%d) \"%s\"\n",
		     netsyncinfo->hostnetmasksize,
		     netsyncinfo->synchostnetmask);

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
DlpWriteNetSyncInfo(int fd,	/* File descriptor */
		    const struct dlp_writenetsyncinfo *netsyncinfo)
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];			/* Argument list */
	static ubyte outbuf[2048];	/* Output buffer */
					/* XXX - Fixed size: bad! */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPCMD_TRACE(1, ">>> WriteNetSyncInfo: mod 0x%02x, LAN %d, name (%d) \"%s\", addr (%d) \"%s\", mask (%d) \"%s\"\n",
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
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(10, "DlpWriteNetSyncInfo: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(fd, DLPCMD_WriteNetSyncInfo, &resp_header, 0, NULL);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Should do something smart, like set dlpcmd_errno
		 * to 'resp_header.errno', and return -1 or something.
		 */
		return -resp_header.errno;

	/* XXX - Check number of response arguments */

	return 0;
}

/* DlpReadFeature
 * Read a feature from the Palm.
 * XXX - What exactly is a feature?
 * XXX - Check to make sure the Palm understands v1.1 of the protocol.
 */
int
DlpReadFeature(int fd,			/* File descriptor */
	       const udword creator,	/* Feature creator */
	       const word featurenum,	/* Number of feature to read */
	       udword *value)		/* Value of feature returned here */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];			/* Argument list */
	static ubyte outbuf[DLPARGLEN_ReadFeature_Req];
					/* Output buffer */
	const ubyte *rptr;	/* Pointer into buffers (for reading) */
	ubyte *wptr;		/* Pointer into buffers (for writing) */

	DLPCMD_TRACE(1, ">>> ReadFeature: creator '%c%c%c%c' (0x%08lx), number %d\n",
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
	err = dlp_send_req(fd, &header, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(10, "DlpReadFeature: waiting for response\n");

	/* Get a response */
	err = dlp_recv_resp(fd, DLPCMD_ReadFeature, &resp_header, 1, argv);
	if (err < 0)
		return err;

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Should do something smart, like set dlpcmd_errno
		 * to 'resp_header.errno', and return -1 or something.
		 */
		return -resp_header.errno;

	/* XXX - Check number of response arguments */
	/* Parse the response argument */
	rptr = argv[0].data;
	*value = get_udword(&rptr);

	DLPCMD_TRACE(3, "Read feature: 0x%08lx (%ld)\n",
		     *value, *value);

	return 0;		/* Success */
}
