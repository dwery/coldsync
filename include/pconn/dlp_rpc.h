/* dlp_rpc.h
 *
 * Definitions and types for Palm RPC calls.
 *
 *	Copyright (C) 2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: dlp_rpc.h,v 1.2 2000-06-18 07:07:47 arensb Exp $
 *
 * Structure of an RPC-over-DLP request:
 *      +------+------+------+------+
 *      | code | argc | reqno| pad  |
 *	+------+------+------+------+
 * Where:
 *	'code' and 'argc' are the same as for any other DLP request: 'code'
 * is the request number (DLPCMD_ProcessRPC); 'argc' is the argument count.
 * Since RPC is weird, I'm not sure this is even used. 1 is a safe value,
 * though.
 *	'reqno' is the number of the specific type of remote request: 0x01
 * for "read memory", 0x0a for "execute system procedure" and so forth.
 *	'pad' is a 0 byte to pad to an even byte boundary.
 *
 * For reqno == 0x0a (execute system procedure), the header just described
 * is followed by:
 *	+------+------+
 *	|     trap    |
 *	+------+------+------+------+
 *	|            D0             |
 *	+------+------+------+------+
 *	|            A0             |
 *	+------+------+------+------+
 *	|  rpc_argc   |
 *	+------+------+
 *	| arguments...|
 *	+------+------+
 * Where:
 *	'trap' is the number of the system trap to execute.
 *	'D0' and 'A0' are values for the D0 and A0 registers. I don't know
 * whether the initial values ever matter or not. At any rate, their values
 * are returned in this space as well.
 *	'rpc_argc' is the number of arguments that follow.
 *	'arguments' is a list of arguments, in the following format:
 *	+------+------+------+------+
 *	| byref| size |   data ...  |
 *	+------+------+------+------+
 *	'byref' is a boolean, indicating whether this argument is passed by
 * reference or not.
 *	'size' is the size of the argument.
 *	'data' is the argument data. This must be padded to an even length.
 *
 * NOTE: The main thing no one tells you is that the arguments must be
 * passed in *reverse* order. So if you want to call a remote procedure
 * that's declared in the Palm headers as
 *	Err SomeFunc(Byte a, Word b, DWord c)
 *		SYS_TRAP(sysTrapSomeFunc);
 * you need to send the following packet:
 *	(ubyte) 0x2d		DLPCMD_ProcessRPC
 *	(ubyte) 1		# arguments in DLP packet
 *	(ubyte) 0x0a		Execute system procedure
 *	(ubyte) 0		Padding
 *	(uword) sysTrapSomeFunc	Trap to execute
 *	(udword) <value>	Register D0
 *	(udword) <value>	Register A0
 *	(uword) 3		# arguments to pass to SomeFunc
 *	(ubyte) 0		3rd arg not by reference
 *	(ubyte) 4		3rd arg size
 *	(ubyte) 0		2nd arg not by reference
 *	(ubyte) 2		2nd arg size
 *	(ubyte) 0		1st arg not by reference
 *	(ubyte) 1		1st arg size
 * I haven't figured out whether there's a hard and fast rule to determine,
 * from the declaration, whether a given argument should be passed by
 * reference or not. If the system function modifies the argument's value,
 * then obviously it needs to be passed by reference. I just don't know of
 * a mechanical way to determine this, though.
 */
#ifndef _dlp_rpc_h_
#define _dlp_rpc_h_

#include "palm_types.h"
#include "PConnection.h"

/* Selected trap numbers. There are more traps than you can shake a stick
 * at. These are only the ones that I've implemented.
 */
#define RPCTRAP_MemHandleNew		0xa01e
#define RPCTRAP_MemMove			0xa026
#define RPCTRAP_BatteryDialog		0xa0bb
#define RPCTRAP_BatteryLevel		0xa248
#define RPCTRAP_PluggedIn		0xa258
#define RPCTRAP_Backlight		0xa2ea
#define RPCTRAP_GetOSVersionString	0xa323
#define RPCTRAP_GetROMToken		0xa340
#define RPCTRAP_MemReadable		0xa23e

/* ROM tokens, for RDLP_ROMToken. Actually, only ROMToken_Snum is used. */
#define ROMToken_Snum	0x736e756d	/* 'snum' */
#define ROMToken_Irda	0x69726461	/* 'irda' */
#define ROMToken_Flex	0x666c6578	/* 'flex' */
#define ROMToken_cd01	0x63643031	/* 'cd01' */
#define ROMToken_cd02	0x63643032	/* 'cd01' */
#define ROMToken_cd03	0x63643033	/* 'cd01' */

#define RPCP_Raw	0		/* Raw data */
#define RPCP_Bool	1		/* Boolean value */
#define RPCP_Byte	2		/* Single-byte value (char) */
#define RPCP_Word	3		/* Word value */
#define RPCP_DWord	4		/* Double-word value */

struct DLPRPC_param {
	Bool byref;			/* Parameter passed by reference? */
	ubyte size;			/* Parameter data size */
	char type;			/* Parameter type: RPCP_* */
	union {
		ubyte bool_v;
		ubyte byte_v;
		uword word_v;
		udword dword_v;
		void *raw_v;
	} data;
};

#ifdef __cplusplus
extern "C" {
#endif	/* __cplusplus */

extern int DlpRPC(struct PConnection *pconn,
		  uword trap,
		  udword *D0,
		  udword *A0,
		  int argc,
		  struct DLPRPC_param *argv);
extern int RDLP_Backlight(struct PConnection *pconn,
			  Bool set,
			  Bool newState);
extern int RDLP_BatteryLevel(struct PConnection *pconn);
extern int RDLP_PluggedIn(struct PConnection *pconn);
extern int RDLP_ROMToken(struct PConnection *pconn,
			 uword cardno,
			 udword token,
			 udword *data_ptr,
			 uword *data_len);
/* XXX - Experimental */
extern int RDLP_MemHandleNew(struct PConnection *pconn,
			     udword size);
extern int RDLP_GetOSVersionString(struct PConnection *pconn);
extern int RDLP_BatteryDialog(struct PConnection *pconn);
extern int RDLP_MemMove(struct PConnection *pconn,
			ubyte *dst,
			const udword src,
			const udword len);
extern int RDLP_MemReadable(struct PConnection *pconn,
			    udword addr);

#ifdef  __cplusplus
}
#endif	/* __cplusplus */

#endif	/* _dlp_rpc_h_ */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
