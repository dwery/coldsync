/* dlp.h
 *
 * Structures and definitions and such for Palm's Desktop Link
 * Protocol (DLP).
 *
 * $Id: dlp.h,v 1.2 1999-01-23 23:06:40 arensb Exp $
 */
#ifndef _dlp_h_
#define _dlp_h_

#include "palm_types.h"

#define DEBUG_DLP
#ifdef DEBUG_DLP

extern int dlp_debug;

#define DLP_TRACE(level, format...)		\
	if (dlp_debug >= (level))		\
		fprintf(stderr, format);

#endif	/* DEBUG_DLP */

#define DLPREQ_RESERVED			0x0f
/* 1.0 functions */
#define DLPREQ_READUSERINFO		0x10
#define DLPREQ_WRITEUSERINFO		0x11
#define DLPREQ_READSYSINFO		0x12
#define DLPREQ_GETSYSDATETIME		0x13
#define DLPREQ_SETSYSDATETIME		0x14
#define DLPREQ_READSTORAGEINFO		0x15
#define DLPREQ_READDBLIST		0x16
#define DLPREQ_OPENDB			0x17
#define DLPREQ_CREATEDB			0x18
#define DLPREQ_CLOSEDB			0x19
#define DLPREQ_DELETEDB			0x1a
#define DLPREQ_READAPPBLOCK		0x1b
#define DLPREQ_WRITEAPPBLOCK		0x1c
#define DLPREQ_READSORTBLOCK		0x1d
#define DLPREQ_WRITESORTBLOCK		0x1e
#define DLPREQ_READNEXTMODIFIEDREC	0x1f
#define DLPREQ_READRECORD		0x20
#define DLPREQ_WRITERECORD		0x21
#define DLPREQ_DELETERECORD		0x22
#define DLPREQ_READRESOURCE		0x23
#define DLPREQ_WRITERESOURCE		0x24
#define DLPREQ_DELETERESOURCE		0x25
#define DLPREQ_CLEANUPDATABASE		0x26
#define DLPREQ_RESETSYNCFLAGS		0x27
#define DLPREQ_CALLAPPLICATION		0x28
#define DLPREQ_RESETSYSTEM		0x29
#define DLPREQ_ADDSYNCLOGENTRY		0x2a
#define DLPREQ_READOPENDBINFO		0x2b
#define DLPREQ_MOVECATEGORY		0x2c
#define DLPREQ_PROCESSRPC		0x2d
#define DLPREQ_OPENCONDUIT		0x2e
#define DLPREQ_ENDOFSYNC		0x2f
#define DLPREQ_RESETRECORDINDEX		0x30
#define DLPREQ_READRECORDIDLIST		0x31
/* 1.1 functions */
#define DLPREQ_READNEXTRECINCATEGORY	0x32
#define DLPREQ_READAPPPREFERENCE	0x33
#define DLPREQ_WRITEAPPPREFERENCE	0x34
#define DLPREQ_READNETSYNCINFO		0x35
#define DLPREQ_WRITENETSYNCINFO		0x36
#define DLPREQ_READFEATURE		0x37

#define DLPREQ_LASTFUNC			0x38	/* Not a function,
						 * just a marker */

/* Error response codes */
#define DLPERR_NONE			0x00
#define DLPERR_SYSTEM			0x01
#define DLPERR_ILLEGALREQ		0x02
#define DLPERR_MEMORY			0x03
#define DLPERR_PARAM			0x04
#define DLPERR_NOTFOUND			0x05
#define DLPERR_NONEOPEN			0x06
#define DLPERR_DATABASEOPEN		0x07
#define DLPERR_TOOMANYOPENDATABASES	0x08
#define DLPERR_ALREADYEXISTS		0x09
#define DLPERR_CANTOPEN			0x0a
#define DLPERR_RECORDDELETED		0x0b
#define DLPERR_RECORDBUSY		0x0c
#define DLPERR_NOTSUPPORTED		0x0d
#define DLPERR_ERRUNUSED1		0x0e
#define DLPERR_READONLY			0x0f
#define DLPERR_NOTENOUGHSPACE		0x10
#define DLPERR_LIMITEXCEEDED		0x11
#define DLPERR_CANCELSYNC		0x12
#define DLPERR_BADWRAPPER		0x13
#define DLPERR_ARGMISSING		0x14
#define DLPERR_ARGSIZE			0x15
#define DLPERR_LASTRESERVED		0x7f

#define DLP_TINY_ARG_MAX		0xffL
#define DLP_SMALL_ARG_MAX		0xffffL
#define DLP_LONG_ARG_MAX		0xffffffffL

struct dlp_req_header
{
	ubyte id;		/* Function ID */
	ubyte argc;		/* # args that follow */
};

struct dlp_resp_header
{
	ubyte id;		/* Function ID (what is this a
				 * response to?) */
	ubyte argc;		/* # args that follow */
	uword errno;		/* Error code */
};

struct dlp_tiny_arg
{
	ubyte id;		/* Argument ID */
	ubyte size;		/* Argument size, 0..255 bytes */
};

struct dlp_small_arg
{
	ubyte id;		/* Argument ID */
	ubyte unused;		/* Dummy for alignment. Set to 0 */
	uword size;		/* Argument size, 0..65535 bytes */
};

/* 2.0 extension: not yet implemented. The Palm will not originate
 * these, but it will accept them.
 */
struct dlp_long_arg
{
	uword id;		/* Argument ID */
	udword size;		/* Argument size, 0..0xffffffff bytes */
};

/* dlp_arg
 * This is a generic argument holder. This is not part of PalmOS or
 * HotSync, but rather a convenience type, for passing to
 * dlp_send_req().
 */
struct dlp_arg
{
	uword id;		/* Argument ID */
	udword size;		/* Argument size, 0..0xffffffff bytes */
	ubyte *data;		/* The argument data itself */
};

/* dlp_time
 * Structure for giving date and time.
 */
struct dlp_time
{
	uword year;		/* Year (4 digits) */
	ubyte month;		/* Month, 1-12 */
	ubyte day;		/* Day of month, 1-31 */
	ubyte hour;		/* Hour, 0-23 */
	ubyte minute;		/* Minute, 0-59 */
	ubyte second;		/* Second, 0-59 */
	ubyte unused;		/* Unused. Set to 0 */
};

/* dlp_sysinfo
 * The data returned from a DlpReadSysInfo command.
 */
struct dlp_sysinfo
{
	udword rom_version;	/* ROM system software version */
	udword localization;	/* Localization ID (?) */
	ubyte unused;		/* Set this to 0 */
	ubyte prodIDsize;	/* Size of product/ID field */
	udword prodID;		/* Product ID */
};

#ifdef __cplusplus
extern "C" {
#endif	/* __cplusplus */

extern char *dlp_errlist[];	/* List of error code meanings */

/* Protocol functions */
extern int dlp_send_req(int fd,
			struct dlp_req_header *header,
			struct dlp_arg *argv);
extern int dlp_read_resp(int fd, struct dlp_resp_header *header,
			 struct dlp_arg **argv_ret);
extern void dlp_free_arglist(int argc, struct dlp_arg *argv);

/* Convenience functions */
extern int DlpEndOfSync(int fd, uword status);
extern int DlpAddSyncLogEntry(int fd, char *msg);
extern int DlpReadSysInfo(int fd, struct dlp_sysinfo *sysinfo);

#ifdef  __cplusplus
}
#endif	/* __cplusplus */
#endif	/* _dlp_h_ */
