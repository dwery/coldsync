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
 * $Id: dlp_cmd.c,v 1.3 1999-01-31 22:04:45 arensb Exp $
 */
#include <stdio.h>
#include "dlp.h"
#include "dlp_cmd.h"
#include "util.h"

#define DLPCMD_DEBUG	1
#ifdef DLPCMD_DEBUG
int dlpcmd_debug = 0;

#define DLPCMD_TRACE(level, format...)		\
	if (dlpcmd_debug >= (level))		\
		fprintf(stderr, "DLPC:" format)

#endif	/* DLPCMD_DEBUG */

int
DlpReadUserInfo(int fd,		/* File descriptor */
		struct dlp_userinfo *userinfo)
				/* Will be filled in with user
				 * information. */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Response argument list */
	ubyte *ptr;			/* Pointer into buffers */

	DLPCMD_TRACE(1, ">>> ReadUserInfo\n");

	/* Fill in the header values */
	header.id = DLPCMD_ReadUserInfo;
	header.argc = 0;

	/* Send the DLP request */
	err = dlp_send_req(fd, &header, NULL);
	/* XXX - Error-checking */

	DLPCMD_TRACE(10, "DlpReadUserInfo: waiting for response\n");
	/* Get a response */
	err = dlp_recv_resp(fd, &resp_header, 1, argv);
	/* XXX - Error-checking. In particular, make sure that the
	 * response ID matches the request.
	 */
	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);

	/* Parse the argument */
	ptr = argv[0].data;
	userinfo->userid = get_udword(&ptr);
	userinfo->viewerid = get_udword(&ptr);
	userinfo->lastsyncPC = get_udword(&ptr);
	userinfo->lastgoodsync.year = get_uword(&ptr);
	userinfo->lastgoodsync.month = get_ubyte(&ptr);
	userinfo->lastgoodsync.day = get_ubyte(&ptr);
	userinfo->lastgoodsync.hour = get_ubyte(&ptr);
	userinfo->lastgoodsync.minute = get_ubyte(&ptr);
	userinfo->lastgoodsync.second = get_ubyte(&ptr);
	get_ubyte(&ptr);		/* Skip padding */
	userinfo->lastsync.year = get_uword(&ptr);
	userinfo->lastsync.month = get_ubyte(&ptr);
	userinfo->lastsync.day = get_ubyte(&ptr);
	userinfo->lastsync.hour = get_ubyte(&ptr);
	userinfo->lastsync.minute = get_ubyte(&ptr);
	userinfo->lastsync.second = get_ubyte(&ptr);
	get_ubyte(&ptr);		/* Skip padding */
	userinfo->usernamelen = get_ubyte(&ptr);
	userinfo->passwdlen = get_ubyte(&ptr);
	memcpy(userinfo->username, ptr, userinfo->usernamelen);
	ptr += userinfo->usernamelen;
	memcpy(userinfo->passwd, ptr, userinfo->passwdlen);
	ptr += userinfo->passwdlen;

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
		debug_dump("DLPC:", userinfo->passwd, userinfo->passwdlen);
	}
#endif	/* DLPCMD_DEBUG */

return err;	/* XXX */
}

int
DlpWriteUserInfo(int fd,	/* File descriptor */
		 struct dlp_setuserinfo *userinfo)
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
	ubyte *ptr;			/* Pointer into buffers */

	DLPCMD_TRACE(1, ">>> WriteUserInfo\n");

	/* Fill in the header values */
	header.id = DLPCMD_WriteUserInfo;
	header.argc = 1;

	/* Construct the argument buffer */
	ptr = outbuf;
	put_udword(&ptr, userinfo->userid);
	put_udword(&ptr, userinfo->viewerid);
	put_udword(&ptr, userinfo->lastsyncPC);
	put_uword(&ptr, userinfo->lastsync.year);
	put_ubyte(&ptr, userinfo->lastsync.month);
	put_ubyte(&ptr, userinfo->lastsync.day);
	put_ubyte(&ptr, userinfo->lastsync.hour);
	put_ubyte(&ptr, userinfo->lastsync.minute);
	put_ubyte(&ptr, userinfo->lastsync.second);
	put_ubyte(&ptr, 0);		/* Padding */
	put_ubyte(&ptr, userinfo->modflags);
	put_ubyte(&ptr, userinfo->usernamelen);
	/* XXX - Probably shouldn't do this unless modifying the user
	 * name.
	 */
	memcpy(ptr, userinfo->username, userinfo->usernamelen);
	ptr += userinfo->usernamelen;

	/* Fill in the argument */
	argv[0].id = DLPARG_WriteUserInfo_UserInfo;
	argv[0].size = ptr - outbuf;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_send_req(fd, &header, argv);
	/* XXX - Error-checking */

	DLPCMD_TRACE(10, "DlpWriteUserInfo: waiting for response\n");
	/* Get a response */
	err = dlp_recv_resp(fd, &resp_header, 0, NULL);
	/* XXX - Error-checking */

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);

	return err;	/* XXX */
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
	ubyte *ptr;			/* Pointer into buffers */

	DLPCMD_TRACE(1, ">>> ReadSysInfo\n");

	/* Fill in the header values */
	header.id = DLPCMD_ReadSysInfo;
	header.argc = 0;

	/* Send the DLP request */
	err = dlp_send_req(fd, &header, NULL);
	/* XXX - Error-checking */

	DLPCMD_TRACE(10, "DlpReadSysInfo: waiting for response\n");
	/* Get a response */
	err = dlp_recv_resp(fd, &resp_header, 1, argv);
	/* XXX - Error-checking. In particular, make sure that the
	 * response ID matches the request.
	 */
	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);

	/* Parse the argument */
	ptr = argv[0].data;
	sysinfo->rom_version = get_udword(&ptr);
	sysinfo->localization = get_udword(&ptr);
	get_ubyte(&ptr);		/* Skip padding */
	sysinfo->prodIDsize = get_ubyte(&ptr);
	sysinfo->prodID = get_udword(&ptr);

	DLPCMD_TRACE(1, "Got sysinfo: ROM version 0x%08lx, loc. 0x%08lx, pIDsize %d, pID 0x%08lx\n",
		     sysinfo->rom_version,
		     sysinfo->localization,
		     sysinfo->prodIDsize,
		     sysinfo->prodID);

return err;	/* XXX */
}

int
DlpGetSysDateTime(int fd,
		  struct dlp_time *ptime)
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Response argument list */
	ubyte *ptr;			/* Pointer into buffers */

	DLPCMD_TRACE(1, ">>> GetSysDateTime\n");

	/* Fill in the header values */
	header.id = DLPCMD_GetSysDateTime;
	header.argc = 0;

	/* Send the DLP request */
	err = dlp_send_req(fd, &header, NULL);
	/* XXX - Error-checking */

	err = dlp_recv_resp(fd, &resp_header, 1, argv);
	/* XXX - Error-checking */
	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);

	/* Parse the response argument */
	ptr = argv[0].data;
	ptime->year = get_uword(&ptr);
	ptime->month = get_ubyte(&ptr);
	ptime->day = get_ubyte(&ptr);
	ptime->hour = get_ubyte(&ptr);
	ptime->minute = get_ubyte(&ptr);
	ptime->second = get_ubyte(&ptr);
	get_ubyte(&ptr);		/* Skip padding */

	DLPCMD_TRACE(1, "System time: %02d:%02d:%02d, %d/%d/%d\n",
		     ptime->hour,
		     ptime->minute,
		     ptime->second,
		     ptime->day,
		     ptime->month,
		     ptime->year);

	return err;	/* XXX */
}

int
DlpSetSysDateTime(int fd,	/* File descriptor */
		  struct dlp_time *ptime)
				/* Time to set */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Argument list */
	static ubyte outbuf[DLPARGLEN_SetSysDateTime_Time];
					/* Buffer holding outgoing arg */
	ubyte *ptr;			/* Pointer into buffers */

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
	ptr = outbuf;
	put_uword(&ptr, ptime->year);
	put_ubyte(&ptr, ptime->month);
	put_ubyte(&ptr, ptime->day);
	put_ubyte(&ptr, ptime->hour);
	put_ubyte(&ptr, ptime->minute);
	put_ubyte(&ptr, ptime->second);
	put_ubyte(&ptr, 0);		/* Padding */

	/* Fill in the argument */
	argv[0].id = DLPARG_SetSysDateTime_Time;
	argv[0].size = DLPARGLEN_SetSysDateTime_Time;
	argv[0].data = outbuf;

	/* Send the DLP request */
	err = dlp_send_req(fd, &header, argv);
	/* XXX - Error-checking */

	DLPCMD_TRACE(10, "DlpSetSysDateTime: waiting for response\n");
	/* Get a response */
	err = dlp_recv_resp(fd, &resp_header, 0, NULL);
	/* XXX - Error-checking */

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
/*  	static ubyte inbuf[2048]; */
	static ubyte outbuf[2048];
	ubyte *ptr;

	DLPCMD_TRACE(1, ">>> ReadStorageInfo(%d)\n", card);

	header.id = DLPCMD_ReadStorageInfo;
	header.argc = 1;

	ptr = outbuf;
	put_ubyte(&ptr, card);		/* Read 0th card */
	put_ubyte(&ptr, 0);		/* Padding */

	argv[0].id = DLPARG_BASE;
	argv[0].size = 2;
	argv[0].data = outbuf;

	err = dlp_send_req(fd, &header, argv);

	err = dlp_recv_resp(fd, &resp_header, 2, argv);

	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);
	if (resp_header.errno != DLPSTAT_NOERR)
		/* XXX - Should do something smart, like set dlpcmd_errno
		 * to 'resp_header.errno', and return -1 or something.
		 */
		return -resp_header.errno;

ptr = argv[0].data;
sinfo.lastcard = get_ubyte(&ptr);
sinfo.more = get_ubyte(&ptr);
/* XXX - sinfo.more is set to 1 in all cases, but none of the
 * subsequent calls to ReadStorageInfo return anything meaningful,
 * either with xcopilot or on a real PalmPilot.
 */ 
get_ubyte(&ptr);	/* Padding */
sinfo.act_count = get_ubyte(&ptr);

cinfo.totalsize = get_ubyte(&ptr);
cinfo.cardno = get_ubyte(&ptr);
cinfo.cardversion = get_uword(&ptr);
cinfo.ctime.year = get_uword(&ptr);
cinfo.ctime.month = get_ubyte(&ptr);
cinfo.ctime.day = get_ubyte(&ptr);
cinfo.ctime.hour = get_ubyte(&ptr);
cinfo.ctime.minute = get_ubyte(&ptr);
cinfo.ctime.second = get_ubyte(&ptr);
get_ubyte(&ptr);
cinfo.rom_size = get_udword(&ptr);
cinfo.ram_size = get_udword(&ptr);
cinfo.free_ram = get_udword(&ptr);
cinfo.cardname_size = get_ubyte(&ptr);
cinfo.manufname_size = get_ubyte(&ptr);
cinfo.cardname = ptr;
ptr += cinfo.cardname_size;
cinfo.manufname = ptr;
ptr += cinfo.manufname_size;

if (resp_header.argc >= 2)
{
ptr = argv[1].data;
extinfo.rom_dbs = get_uword(&ptr);
extinfo.ram_dbs = get_uword(&ptr);
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
ubyte *ptr;

	DLPCMD_TRACE(1, ">>> ReadDBList flags 0x%02x, card %d, start %d\n",
		     flags, card, start);

	header.id = DLPCMD_ReadDBList;
	header.argc = 1;

	/* Construct request header in 'outbuf' */
	ptr = outbuf;
	put_ubyte(&ptr, flags);
	put_ubyte(&ptr, card);
	put_uword(&ptr, start);

	argv[0].id = DLPARG_BASE;
	argv[0].size = 4;
	argv[0].data = outbuf;

	err = dlp_send_req(fd, &header, argv);
	err = dlp_recv_resp(fd, &resp_header, 1, argv);

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
ptr = argv[0].data;
list_header.lastindex = get_uword(&ptr);
list_header.flags = get_ubyte(&ptr);
list_header.act_count = get_ubyte(&ptr);
fprintf(stderr, "List header: last %d, flags 0x%02x, count %d\n",
	list_header.lastindex,
	list_header.flags,
	list_header.act_count);

/* Parse the info for this database */
dbinfo.size = get_ubyte(&ptr);
dbinfo.misc_flags = get_ubyte(&ptr);
dbinfo.db_flags = get_uword(&ptr);
dbinfo.type = get_udword(&ptr);
dbinfo.creator = get_udword(&ptr);
dbinfo.version = get_uword(&ptr);
dbinfo.modnum = get_udword(&ptr);
dbinfo.ctime.year = get_uword(&ptr);
dbinfo.ctime.month = get_ubyte(&ptr);
dbinfo.ctime.day = get_ubyte(&ptr);
dbinfo.ctime.hour = get_ubyte(&ptr);
dbinfo.ctime.minute = get_ubyte(&ptr);
dbinfo.ctime.second = get_ubyte(&ptr);
get_ubyte(&ptr);
dbinfo.mtime.year = get_uword(&ptr);
dbinfo.mtime.month = get_ubyte(&ptr);
dbinfo.mtime.day = get_ubyte(&ptr);
dbinfo.mtime.hour = get_ubyte(&ptr);
dbinfo.mtime.minute = get_ubyte(&ptr);
dbinfo.mtime.second = get_ubyte(&ptr);
get_ubyte(&ptr);
dbinfo.baktime.year = get_uword(&ptr);
dbinfo.baktime.month = get_ubyte(&ptr);
dbinfo.baktime.day = get_ubyte(&ptr);
dbinfo.baktime.hour = get_ubyte(&ptr);
dbinfo.baktime.minute = get_ubyte(&ptr);
dbinfo.baktime.second = get_ubyte(&ptr);
get_ubyte(&ptr);
dbinfo.index = get_uword(&ptr);

fprintf(stderr, "Database info:\n");
fprintf(stderr, "\tsize %d, misc flags 0x%02x, DB flags 0x%04x\n",
	dbinfo.size,
	dbinfo.misc_flags,
	dbinfo.db_flags);
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
	ptr);

/*  return 0; */
return list_header.flags;
}

/* DlpAddSyncLogEntry
 *
 * It appears that only the first entry takes: subsequent ones get
 * ignored. Hence, you have to transmit your entire log at once.
 * Bleah.
 */
int
DlpAddSyncLogEntry(int fd,	/* File descriptor */
		   char *msg)	/* Log message */
{
	int err;
	struct dlp_req_header header;		/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Argument list */

	DLPCMD_TRACE(1, ">>> AddSyncLogEntry \"%s\"\n", msg);

	/* Fill in the header values */
	header.id = DLPCMD_AddSyncLogEntry;
	header.argc = 1;

	/* Fill in the argument */
	argv[0].id = DLPARG_AddSyncLogEntry_Msg;
	argv[0].size = strlen(msg)+1;	/* Include the final NUL */
	argv[0].data = msg;

	/* Send the DLP request */
	err = dlp_send_req(fd, &header, argv);
	/* XXX - Error-checking */

	DLPCMD_TRACE(10, "DlpAddSyncLogEntry: waiting for response\n");
	/* Get a response */
	err = dlp_recv_resp(fd, &resp_header, 0, argv);
	/* XXX - Error-checking. In particular, make sure that the
	 * response ID matches the request.
	 */
	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);

return err;	/* XXX */
}

int
DlpEndOfSync(int fd,		/* File descriptor */
	     dlpEndOfSyncStatus status)
				/* Exit status, reason for termination */
{
	int err;
	struct dlp_req_header header;	/* Request header */
	struct dlp_resp_header resp_header;	/* Response header */
	struct dlp_arg argv[1];		/* Argument list */
	static ubyte status_buf[2];	/* Buffer for the status word */
	ubyte *ptr;			/* Pointer into buffers */
uword resp_status;

	DLPCMD_TRACE(1, ">>> EndOfSync status %d\n", status);

	/* Fill in the header values */
	header.id = DLPCMD_EndOfSync;
	header.argc = 1;

	/* Fill in the argument */
	argv[0].id = DLPARG_EndOfSync_Status;
	argv[0].size = DLPARGLEN_EndOfSync_Status;
	ptr = status_buf;
	put_uword(&ptr, status);
	argv[0].data = status_buf;

	/* Send the DLP request */
	err = dlp_send_req(fd, &header, argv);

	/* XXX - Get a response */
	err = dlp_recv_resp(fd, &resp_header, 1, argv);
	/* XXX - Error-checking. In particular, make sure that the
	 * response ID matches the request.
	 */
	DLPCMD_TRACE(2, "Got response, id 0x%02x, args %d, status %d\n",
		     resp_header.id,
		     resp_header.argc,
		     resp_header.errno);
	ptr = argv[0].data;
	resp_status = get_uword(&ptr);
DLPCMD_TRACE(6, "EndOfSync status: %d\n", resp_status);
	
return err;	/* XXX */
}
