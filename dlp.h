/* dlp.h
 *
 * Structures and definitions and such for Palm's Desktop Link
 * Protocol (DLP).
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
 * $Id: dlp.h,v 1.3 1999-01-31 21:59:10 arensb Exp $
 */
#ifndef _dlp_h_
#define _dlp_h_

#include "palm_types.h"

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
#define DLPSTAT_NOERR		0x00	/* No error */
#define DLPSTAT_SYSTEM		0x01	/* General Palm system error */
#define DLPSTAT_ILLEGALREQ	0x02	/* Unknown request ID */
#define DLPSTAT_NOMEM		0x03	/* Insufficient memory */
#define DLPSTAT_PARAM		0x04	/* Invalid parameter */
#define DLPSTAT_NOTFOUND	0x05	/* Database, record or
					 * resource not found */
#define DLPSTAT_NONEOPEN	0x06	/* There are no open databases */
#define DLPSTAT_DBOPEN		0x07	/* Database is open by someone else */
#define DLPSTAT_TOOMANYOPEN	0x08	/* Too many open databases */
#define DLPSTAT_EXISTS		0x09	/* Database already exists */
#define DLPSTAT_CANTOPEN	0x0a	/* Can't open database */
#define DLPSTAT_RECDELETED	0x0b	/* Record is deleted */
#define DLPSTAT_RECBUSY		0x0c	/* Record is busy */
#define DLPSTAT_UNSUPP		0x0d	/* Requested operation is not
					 * supported on the given
					 * database type */
#define DLPSTAT_UNUSED1		0x0e
#define DLPSTAT_READONLY	0x0f	/* You do not have write
					 * access, or database is in
					 * ROM */
#define DLPSTAT_SPACE		0x10	/* Not enough space for
					 * record/resource/whatever */
#define DLPSTAT_LIMIT		0x11	/* Size limit exceeded */
#define DLPSTAT_CANCEL		0x12	/* Cancel the sync */
#define DLPSTAT_BADWRAP		0x13	/* Bad arg wrapper */
#define DLPSTAT_NOARG		0x14	/* Required argument not found */
#define DLPSTAT_ARGSIZE		0x15	/* Invalid argument size */

/* Error codes */
#define DLPERR_NOERR		0	/* No error */

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
	uword errno;		/* Error code */
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

/* Protocol functions */
extern int dlp_send_req(int fd, struct dlp_req_header *header,
			struct dlp_arg argv[]);
extern int dlp_recv_resp(int fd, struct dlp_resp_header *header,
			 int argc,
			 struct dlp_arg argv[]);
#endif	/* _dlp_h_ */
