/* dlp.h
 *
 * Structures and definitions and such for Palm's Desktop Link
 * Protocol (DLP).
 *
 *	Copyright (C) 1999-2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * Structure of a DLP request:
 *	[request header] [arg1] [arg2]...
 * A DLP request header is of the form
 *	+------+------+
 *	| code | argc |
 *	+------+------+
 * where 'code' identifies the request, and 'argc' is the number of
 * arguments following the header.
 *
 * Argument headers come in three sizes: tiny, small and long,
 * depending on the length of the data they contain. Tiny arguments
 * have <= 256 bytes of payload, small arguments can hold up to 64Kb
 * of payload, and long arguments can hold up to 4Gb of payload (long
 * arguments are not yet implemented, but are anticipated for DLP
 * v2.0). The three types of arguments look like:
 *
 * Tiny:
 *	+------+------+------+------+------+------+
 *	|  id  | size | <size> bytes of data      |
 *	+------+------+------+------+------+------+
 * Small:
 *	+------+------+------+------+
 *	|  id  |unused| size        |
 *	+------+------+------+------+
 *	| <size> bytes of data      |
 *	+------+------+------+------+
 * Long:
 *	+------+------+------+------+------+------+
 *	|  id         | size                      |
 *	+------+------+------+------+------+------+
 *	| <size> bytes of data                    |
 *	+------+------+------+------+------+------+
 * where 'id' identifies the argument (presumably, under DLP, argument
 * order doesn't matter, since each argument contains a
 * self-describing ID), and 'size' gives the size of the data.
 *
 * The response to a DLP request is of the form:
 *	[response header] [arg1] [arg2]...
 * A DLP response header is of the form
 *	+------+------+------+------+
 *	| code | argc | errno       |
 *	+------+------+------+------+
 * where 'code' identifies the request that this is in response to
 * (this is the request code, with the high bit set), 'argc' is the
 * number of arguments following the header, and 'errno' is an error
 * code to give the status of the response (0 means "no error").
 * The arguments that follow the header are of the same types as those
 * in the request. However, even in DLP 2.0, the Palm is not expected
 * to originate long arguments, even though it will accept them.
 *
 * $Id: dlp.h,v 1.6 2001-09-08 00:20:42 arensb Exp $
 */
#ifndef _dlp_h_
#define _dlp_h_

#include "palm.h"

/* These define the version of the DLP protocol that this library
 * implements.
 */
#define DLP_VER_MAJOR		1
#define DLP_VER_MINOR		2

/* Lengths of various structures */
#define DLP_REQHEADER_LEN	2	/* Length of request header */
#define DLP_RESPHEADER_LEN	4	/* Length of response header */
#define DLP_TINYARG_LEN		2	/* Length of tiny argument */
#define DLP_TINYARG_MAXLEN	0xff	/* Max. length of tiny argument data */
#define DLP_SMALLARG_LEN	4	/* Length of small argument */
#define DLP_SMALLARG_MAXLEN	0xffff	/* Max. length of small argument data */
#define DLP_LONGARG_LEN		6	/* Length of long argument */
#define DLP_LONGARG_MAXLEN	0xffffffffL;
					/* Max. length of long argument data */

/* DLP response status codes */
typedef enum {
	DLPSTAT_NOERR		= 0x00,	/* No error */
	DLPSTAT_SYSTEM		= 0x01,	/* General Palm system error */
	DLPSTAT_ILLEGALREQ	= 0x02,	/* Unknown request ID */
	DLPSTAT_NOMEM		= 0x03,	/* Insufficient memory */
	DLPSTAT_PARAM		= 0x04,	/* Invalid parameter */
	DLPSTAT_NOTFOUND	= 0x05,	/* Database, record or
					 * resource not found */
	DLPSTAT_NONEOPEN	= 0x06,	/* There are no open databases */
	DLPSTAT_DBOPEN		= 0x07,	/* Database is open by someone else */
	DLPSTAT_TOOMANYOPEN	= 0x08,	/* Too many open databases */
	DLPSTAT_EXISTS		= 0x09,	/* Database already exists */
	DLPSTAT_CANTOPEN	= 0x0a,	/* Can't open database */
	DLPSTAT_RECDELETED	= 0x0b,	/* Record is deleted */
	DLPSTAT_RECBUSY		= 0x0c,	/* Record is busy */
	DLPSTAT_UNSUPP		= 0x0d,	/* Requested operation is not
					 * supported on the given
					 * database type */
	DLPSTAT_UNUSED1		= 0x0e,
	DLPSTAT_READONLY	= 0x0f,	/* You do not have write
					 * access, or database is in
					 * ROM */
	DLPSTAT_SPACE		= 0x10,	/* Not enough space for
					 * record/resource/whatever */
	DLPSTAT_LIMIT		= 0x11,	/* Size limit exceeded */
	DLPSTAT_CANCEL		= 0x12,	/* Cancel the sync */
	DLPSTAT_BADWRAP		= 0x13,	/* Bad arg wrapper */
	DLPSTAT_NOARG		= 0x14,	/* Required argument not found */
	DLPSTAT_ARGSIZE		= 0x15	/* Invalid argument size */
} dlp_stat_t;

/* dlp_req_header
 * DLP request header.
 */
struct dlp_req_header
{
	ubyte id;		/* Function ID */
	ubyte argc;		/* # args that follow */
};

/* dlp_resp_header
 * DLP response header.
 */
struct dlp_resp_header
{
	ubyte id;		/* Function ID (what is this a
				 * response to?) */
	ubyte argc;		/* # args that follow */
	uword error;		/* Error code */
};

/* dlp_tiny_arg
 * Tiny argument, for values < 256 bytes.
 */
struct dlp_tiny_arg
{
	ubyte id;		/* Argument ID */
	ubyte size;		/* Argument size, 0..255 bytes */
};

/* dlp_small_arg
 * Small argument, for values between 256 bytes and 64Kb in length.
 */
struct dlp_small_arg
{
	ubyte id;		/* Argument ID */
	ubyte unused;		/* Dummy for alignment. Set to 0 */
	uword size;		/* Argument size, 0..65535 bytes */
};

/* dlp_long_arg
 * Long argument, for values longer than 64Kb.
 *
 * 2.0 extension: not yet implemented. The Palm will not originate
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

struct PConnection;		/* Forward declaration */

extern int dlp_init(struct PConnection *pconn);
extern int dlp_tini(struct PConnection *pconn);

/* Protocol functions */
extern int dlp_send_req(struct PConnection *pconn,
			struct dlp_req_header *header,
			struct dlp_arg argv[]);
extern int dlp_recv_resp(struct PConnection *pconn, const ubyte id,
			 struct dlp_resp_header *header,
			 const struct dlp_arg **argv);
#endif	/* _dlp_h_ */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
