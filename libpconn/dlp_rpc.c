/* dlp_rpc.c
 *
 * Convenience functions: RPC calls over DLP.
 *
 *	Copyright (C) 2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * See description of RPC-over-DLP in <include/pconn/dlp_rpc.h>.
 *
 * $Id: dlp_rpc.c,v 1.1 2000-04-09 14:16:45 arensb Exp $
 */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>

#if HAVE_LIBINTL
#  include <libintl.h>		/* For i18n */
#endif	/* HAVE_LIBINTL */

#include "pconn/PConnection.h"
#include "pconn/util.h"
#include "pconn/dlp_rpc.h"

extern int dlpc_trace;		/* Debugging level for DLP commands */

#define DLPC_TRACE(n)	if (dlpc_trace >= (n))

/* DlpRPC
 * This is the main function in this file, the one that actually
 * implements the RPC calls. Everything else in this file is a wrapper
 * around DlpRPC().
 * Returns 0 if successful, or a negative value in case of error (this is
 * different from the call succeeding, but returning an error value).
 * The values of the D0 and A0 registers are returned in D0 and A0. Any
 * arguments that were passed by reference are returned in argv[].
 */
int
DlpRPC(struct PConnection *pconn,	/* Connection to Palm */
       uword trap,			/* RPC trap number */
       udword *D0,			/* Used for return value (?) */
       udword *A0,			/* Used for return value (?) */
       int argc,			/* # parameters */
       struct DLPRPC_param *argv)	/* List of parameters */
{
	int err;
	int i;
	static ubyte outbuf[4096];	/* XXX - Fixed size: bad */
	const ubyte *inbuf;
	const ubyte *rptr;
	ubyte *wptr;
	uword inlen;
	uword dlp_err;			/* DLP return value */
	uword ret_argc;			/* # arguments in return packet
					 * (Should be the same as in the
					 * sent packet).
					 */

	DLPC_TRACE(3)
		fprintf(stderr, ">>> RPC: trap 0x%04x, D0 0x%08lx, A0 0x%08lx "
			"argc %d\n",
			trap, *D0, *A0, argc);

	/* Construct DLP header */
	wptr = outbuf;
	put_ubyte(&wptr, DLPCMD_ProcessRPC);
	put_ubyte(&wptr, 1);		/* # arguments */
					/* XXX - Is this even used? */

	/* Construct RPC header */
	put_ubyte(&wptr, 0x0a);		/* RPC request */
					/* XXX - Define a constant */
	put_ubyte(&wptr, 0);		/* padding */
	put_uword(&wptr, trap);		/* trap number */
	put_udword(&wptr, *D0);
	put_udword(&wptr, *A0);
	put_uword(&wptr, (uword) argc);	/* # parameters in call */
	DLPC_TRACE(5)
	{
		fprintf(stderr, "RPC header:\n");
		debug_dump(stderr, "RPCH", outbuf, wptr-outbuf);
	}

	/* Append RPC parameters */
	for (i = 0; i < argc; i++)
	{
		switch (argv[i].type)
		{
		    case RPCP_Raw:
			DLPC_TRACE(6)
			{
				fprintf(stderr,
					"RPC param (%sraw, size = %d):\n",
					(argv[i].byref ? "ref " : ""),
					argv[i].size);
				debug_dump(stderr, ":", argv[i].data.raw_v,
					   argv[i].size);
			}
			put_ubyte(&wptr, argv[i].byref);
			put_ubyte(&wptr, argv[i].size);
			memcpy(wptr, argv[i].data.raw_v, argv[i].size);
			wptr += argv[i].size;
			if (argv[i].size & 1)
				/* Pad to even byte boundary */
				put_ubyte(&wptr, 0);
			break;

		    case RPCP_Bool:
			DLPC_TRACE(6)
				fprintf(stderr, "RPC param (%sbool): %d\n",
					(argv[i].byref ? "ref " : ""),
					argv[i].data.bool_v);
			put_ubyte(&wptr, argv[i].byref);	/* By ref */
			put_ubyte(&wptr, 1);			/* Size */
			put_ubyte(&wptr, argv[i].data.bool_v);
			put_ubyte(&wptr, 0);			/* Padding */
			break;

		    case RPCP_Byte:
			DLPC_TRACE(6)
				fprintf(stderr, "RPC param (%sbyte): 0x%02x\n",
					(argv[i].byref ? "ref " : ""),
					argv[i].data.byte_v);
			put_ubyte(&wptr, argv[i].byref);	/* By ref */
			put_ubyte(&wptr, 1);			/* Size */
			put_ubyte(&wptr, argv[i].data.byte_v);
			put_ubyte(&wptr, 0);			/* Padding */
			break;

		    case RPCP_Word:
			DLPC_TRACE(6)
				fprintf(stderr, "RPC param (%sword): 0x%04x\n",
					(argv[i].byref ? "ref " : ""),
					argv[i].data.word_v);
			put_ubyte(&wptr, argv[i].byref);	/* By ref */
			put_ubyte(&wptr, 2);			/* Size */
			put_uword(&wptr, argv[i].data.word_v);
			break;

		    case RPCP_DWord:
			DLPC_TRACE(6)
				fprintf(stderr,
					"RPC param (%sDWord): 0x%08lx\n",
					(argv[i].byref ? "ref " : ""),
					argv[i].data.dword_v);
			put_ubyte(&wptr, argv[i].byref);	/* By ref */
			put_ubyte(&wptr, 4);			/* Size */
			put_udword(&wptr, argv[i].data.dword_v);
			break;

		    default:
			/* XXX - Barf */
			break;
		}
	}

	/* Now send this whole thing off as a DLP packet.
	 * Can't use dlp_send_req() because the format of the request is
	 * different from other DLP requests; in particular, the arguments
	 * don't have the same structure.
	 */
	DLPC_TRACE(5)
	{
		fprintf(stderr, "Sending RPC request:\n");
		debug_dump(stderr, "RPC>", outbuf, wptr-outbuf);
	}

	err = padp_write(pconn, outbuf, wptr-outbuf);
	if (err < 0)
	{
		fprintf(stderr, _("%s: Error: padp_write() returned %d\n"),
			"DlpRPC",
			err);
/*  		free(outbuf); */
		return err;
	}

	/* Get response */
	err = padp_read(pconn, &inbuf, &inlen);
	DLPC_TRACE(5)
		fprintf(stderr, "Got response. Err == %d\n", err);
	DLPC_TRACE(6)
		debug_dump(stderr, "RPC<", inbuf, inlen);

	/* Parse the response */
	/* XXX - It might be nice to trace a lot of this */
	/* Parse the DLP response header */
	rptr = inbuf;
	get_ubyte(&rptr);		/* Should be DLPCMD_ProcessRPC |
					 * 0x80 */
	get_ubyte(&rptr);		/* Should be the DLP argument
					 * count. Always 1. */
	dlp_err = get_uword(&rptr);	/* Return value for DLP request.
					 * XXX - Trace this.
					 */
	/* Parse the RPC header inside the DLP packet */
	get_ubyte(&rptr);		/* It seems that this should be
					 * 0x0a | 0x80 == 0x8a, but it
					 * always seems to be 0.
					 */
	get_ubyte(&rptr);		/* Padding */
	get_uword(&rptr);		/* Trap number */
	*D0 = get_udword(&rptr);	/* Contents of register D0 */
	*A0 = get_udword(&rptr);	/* Contents of register A0 */
	DLPC_TRACE(5)
		fprintf(stderr, "D0 == 0x%08lx, A0 == 0x%08lx\n", *D0, *A0);
	ret_argc = get_uword(&rptr);	/* # arguments in return packet.
					 * Should be the same as in packet
					 * that was sent.
					 * XXX - Check this.
					 */

	/* Parse the RPC arguments */
	for (i = 0; i < ret_argc; i++)
	{
		ubyte byref;		/* Was this argument passed by
					 * reference? */
		ubyte arg_len;		/* Length of this argument, in
					 * bytes */

		byref = get_ubyte(&rptr);
		arg_len = get_ubyte(&rptr);

		DLPC_TRACE(7)
		{
			fprintf(stderr, "Returned arg %d\n", i);
			fprintf(stderr, "byref == 0x%02x\n", byref);
			fprintf(stderr, "arg_len == 0x%02x\n", arg_len);
		}

		/* If the argument was passed by reference, copy it back
		 * into 'argv'. Otherwise, just skip over it.
		 */
		if (!byref)
		{
			rptr += arg_len + (arg_len & 1);
					/* Arguments must be aligned on
					 * even byte boundary.
					 */
			continue;	/* Skip to next argument */
		}

		/* Copy this argument back into 'argv[i]'. */
		/* XXX - Might be a good thing to make sure the argument
		 * sizes match: arg_len == argv[i].size.
		 */
		switch (arg_len)
		{
		    case 1:		/* Bool or ubyte */
			argv[i].data.byte_v = get_ubyte(&rptr);
			DLPC_TRACE(6)
				fprintf(stderr,
					"Return argv[%d] == byte 0x%02x\n",
					i, argv[i].data.byte_v);
			get_ubyte(&rptr);	/* Padding */
			break;

		    case 2:		/* Uword */
			argv[i].data.word_v = get_uword(&rptr);
			DLPC_TRACE(6)
				fprintf(stderr,
					"Return argv[%d] == word 0x%04x\n",
					i, argv[i].data.word_v);
			break;

		    case 4:		/* DWord */
			argv[i].data.dword_v = get_udword(&rptr);
			DLPC_TRACE(6)
				fprintf(stderr,
					"Return argv[%d] == dword 0x%08lx\n",
					i, argv[i].data.dword_v);
			break;

		    default:		/* String or something */
			memcpy(argv[i].data.raw_v, rptr, arg_len);
			rptr += arg_len + (arg_len & 1);

			DLPC_TRACE(6)
			{
				fprintf(stderr,
					"Return argv[%d] == raw, %d bytes:\n",
					i, arg_len);
				debug_dump(stderr, "RET", argv[i].data.raw_v,
					   arg_len);
			}
			break;
		}
	}

	return 0;
}

#if 0
/* XXX - Bleah. This doesn't appear to barf, but neither does it return
 * sane values. Plus, what it does return appears to be byte-swapped, as if
 * it were reading a series of word-sized values.
 */
int
DlpRPC_ReadMem(struct PConnection *pconn,
	       udword ptr,		/* Starting address */
	       uword len,		/* Length to read */
	       ubyte *buf)		/* Where to put the results */
{
	int err;
/*  	int i; */
	static ubyte outbuf[4096];	/* XXX - Fixed size: bad */
		/* XXX - Actually, you can only read/write up to 256 bytes
		 * at a time with ReadMem/WriteMem, so can just make this
		 * the maximum size.
		 */
	const ubyte *inbuf;
/*  	const ubyte *rptr; */
	ubyte *wptr;
	uword inlen;

	DLPC_TRACE(3)
		fprintf(stderr, "Inside DlpRPC_ReadMem(0x%08lx, %d)\n",
			ptr, len);

	/* Construct DLP header */
	wptr = outbuf;
	put_ubyte(&wptr, DLPCMD_ProcessRPC);
	put_ubyte(&wptr, 1);		/* # arguments */
					/* XXX - Is this even used? */

	/* Construct RPC header */
	put_ubyte(&wptr, 0x01);		/* RPC request */
					/* XXX - Define a constant */
	put_ubyte(&wptr, 0);		/* padding */
	put_udword(&wptr, ptr);		/* Starting address to read */
	put_uword(&wptr, len);		/* # bytes to read */

	/* Now send this whole thing off as a DLP packet. */
	DLPC_TRACE(5)
	{
		fprintf(stderr, "Sending ReadMem request:\n");
		debug_dump(stderr, "RPC>", outbuf, wptr-outbuf);
	}
	err = padp_write(pconn, outbuf, /*wptr-outbuf*/4+len);
fprintf(stderr, "RDLP_ReadMem: After padp_write()\n");
	if (err < 0)
	{
		fprintf(stderr, _("%s: Error: padp_write() returned %d\n"),
			"RDLP_ReadMem",
			err);
/*  		free(outbuf); */
		return err;
	}

	/* Get response */
	err = padp_read(pconn, &inbuf, &inlen);
	/* XXX - Barf if err < 0 */
fprintf(stderr, "RDLP_ReadMem: After padp_read()\n");
	DLPC_TRACE(5)
		fprintf(stderr, "Got response. Err == %d\n", err);
	DLPC_TRACE(6)
		debug_dump(stderr, "RPC<", inbuf, inlen);

	return 0;
}
#endif	/* 0 */

/* RDLP_Backlight
 * Queries and optionally sets the backlight.
 * Returns 0 if the backlight was turned off just before this call, or 1 if
 * the backlight was on.
 * If 'set' is true, then this function will turn the backlight on or off,
 * as indicated by 'newState'. If 'set' is false, this function will merely
 * find out whether the backlight is on or off.
 * For 'newState', True means "on", False means "off". If 'set' is false,
 * the value of 'newState' has no effect.
 */
int
RDLP_Backlight(struct PConnection *pconn,
	       Bool set,
	       Bool newState)
{
	int err;
	udword D0;
	udword A0;
	struct DLPRPC_param argv[2];

	DLPC_TRACE(5)
		fprintf(stderr, "Inside RDLP_Backlight(%d, %d)\n",
			set, newState);
	D0 = A0 = 0L;

	/* Arg 0: Boolean set */
	argv[1].byref = False;
	argv[1].type = RPCP_Bool;
	argv[1].data.bool_v = set;

	/* Arg 1: Boolean newState */
	argv[0].byref = False;
	argv[0].type = RPCP_Bool;
	argv[0].data.bool_v = newState;

	err = DlpRPC(pconn, RPCTRAP_Backlight, &D0, &A0, 2, argv);
	DLPC_TRACE(5)
		fprintf(stderr, "RDLP_Backlight: err == %d\n", err);
	/* XXX - Return -1 if err < 0? */

	return D0;	/* Result returned in D0 */
}

/* RDLP_BatteryLevel
 * Returns the battery level, as a number between 0 (empty) and 255 (fully
 * charged).
 */
int
RDLP_BatteryLevel(struct PConnection *pconn)
{
	int err;
	udword D0;
	udword A0;

	DLPC_TRACE(5)
		fprintf(stderr, "Inside RDLP_BatteryLevel()\n");
	D0 = A0 = 0L;

	err = DlpRPC(pconn, RPCTRAP_BatteryLevel, &D0, &A0, 0, NULL);
	DLPC_TRACE(5)
		fprintf(stderr, "RDLP_BatteryLevel: err == %d\n", err);
	/* XXX - Return -1 if err < 0? */

	return D0;	/* Result returned in D0 */
}

/* RDLP_PluggedIn
 * I'm not sure what this does. I think it tells you whether the Palm is in
 * its cradle. Of course, if you're finding this out through an RPC, it's
 * trivially true.
 */
int
RDLP_PluggedIn(struct PConnection *pconn)
{
	int err;
	udword D0;
	udword A0;

	DLPC_TRACE(5)
		fprintf(stderr, "Inside RDLP_PluggedIn()\n");
	D0 = A0 = 0L;

	err = DlpRPC(pconn, RPCTRAP_PluggedIn, &D0, &A0, 0, NULL);
	DLPC_TRACE(5)
		fprintf(stderr, "RDLP_PluggedIn: err == %d\n", err);
	/* XXX - Return -1 if err < 0? */

	return D0;	/* Result returned in D0 */
}

int
RDLP_GetOSVersionString(struct PConnection *pconn)
{
	int err;
	udword D0;
	udword A0;

	DLPC_TRACE(5)
		fprintf(stderr, "Inside RDLP_GetOSVersionString()\n");
	D0 = A0 = 0L;

	err = DlpRPC(pconn, RPCTRAP_GetOSVersionString, &D0, &A0, 0, NULL);
	DLPC_TRACE(5)
		fprintf(stderr, "RDLP_GetOSVersionString: err == %d\n", err);
	/* XXX - Return -1 if err < 0? */

	return D0;	/* Result returned in D0 */
}

/* RDLP_MemMove
 * Read 'len' bytes starting at address 'src' on the Palm, and copy them to
 * the location specified by 'dst' (on this host). This is a good way to
 * read the contents of a memory location on the Palm.
 */
int
RDLP_MemMove(struct PConnection *pconn,
	     ubyte *dst,
	     const udword src,
	     const udword len)
{
	int err;
	udword D0;
	udword A0;
	struct DLPRPC_param argv[3];

	DLPC_TRACE(5)
		fprintf(stderr, "Inside RDLP_MemMove(<buf>, 0x%08lx, %ld)\n",
			src, len);
	D0 = A0 = 0L;

	/* Arg 0: const VoidPtr dstP */
	argv[2].byref = True;
	argv[2].type = RPCP_Raw;
	argv[2].size = len;
	argv[2].data.raw_v = dst;

	/* Arg 1: const void *const sP */
	argv[1].byref = False;
	argv[1].type = RPCP_DWord;
	argv[1].data.dword_v = src;

	/* Arg 2: ULong numBytes */
	argv[0].byref = False;
	argv[0].type = RPCP_DWord;
	argv[0].data.dword_v = len;

	err = DlpRPC(pconn, RPCTRAP_MemMove, &D0, &A0, 3, argv);
	if (err < 0)
	{
		fprintf(stderr, _("%s: Error in DlpRPC()\n"),
			"RDLP_MemMove");
		return -1;
	}

	return 0;
}

int
RDLP_BatteryDialog(struct PConnection *pconn)
{
	int err;
	udword D0;
	udword A0;

fprintf(stderr, "Inside RDLP_BatteryDialog()\n");
	D0 = A0 = 0L;

	err = DlpRPC(pconn, RPCTRAP_BatteryDialog, &D0, &A0, 0, NULL);
fprintf(stderr, "RDLP_BatteryDialog: err == %d\n", err);

	return 0;
}

int
RDLP_MemHandleNew(struct PConnection *pconn,
		  udword size)
{
	int err;
	udword D0;
	udword A0;
	struct DLPRPC_param argv[1];

fprintf(stderr, "Inside MemHandleNew(%ld)\n", size);
	D0 = A0 = 0L; 

	/* Arg 0: ULong size */
	argv[0].byref = True;
	argv[0].type = RPCP_DWord;
	argv[0].data.dword_v = size;

	err = DlpRPC(pconn, RPCTRAP_MemHandleNew, &D0, &A0, 1, argv);
fprintf(stderr, "RDLP_MemHandleNew: err == %d\n", err);
fprintf(stderr, "D0 == 0x%08lx, A0 == 0x%08lx\n", D0, A0);
return 0;
}

/* RDLP_ROMToken
 * Read one of the ROM tokens, e.g., 'snum' (the Palm's serial number in
 * ROM).
 * Note that this function by itself is not sufficient to get the serial
 * number: first you need to call RDLP_ROMToken() to get 'data_ptr' and
 * 'data_len'. 'data_ptr' is a pointer to the data on the Palm. You then
 * need to read 'data_len' bytes in the Palm's memory, starting at
 * 'data_ptr', to get the actual token.
 */
int
RDLP_ROMToken(struct PConnection *pconn,
	      uword cardno,		/* Memory card number */
	      udword token,		/* The token to read. See the
					 * ROMToken_* constants. */
	      udword *data_ptr,		/* Palm pointer to data returned
					 * here */
	      uword *data_len)		/* Length of data returned here */
{
	int err;
	udword D0;
	udword A0;
	struct DLPRPC_param argv[4];

	DLPC_TRACE(3)
		fprintf(stderr, "Inside RDLP_ROMToken()\n");
	D0 = A0 = 0L; 

	/* Arg 0: Word cardNo */
	argv[3].byref = False;
	argv[3].type = RPCP_Word;
	argv[3].data.word_v = 0;

	/* Arg 1: DWord token */
	argv[2].byref = False;
	argv[2].type = RPCP_DWord;
	argv[2].data.dword_v = token;

	/* Arg 2: BytePtr *dataP */
	argv[1].byref = True;
	argv[1].type = RPCP_DWord;
	argv[1].data.dword_v = 0L;

	/* Arg 3: WordPtr sizeP */
	argv[0].byref = True;
	argv[0].type = RPCP_Word;
	argv[0].data.word_v = 0;

	err = DlpRPC(pconn, RPCTRAP_GetROMToken, &D0, &A0, 4, argv);
	/* XXX - Barf if err != 0 */

	*data_ptr = argv[1].data.dword_v;
	*data_len = argv[0].data.word_v;

	return 0;
}

int
RDLP_MemReadable(struct PConnection *pconn,
		 udword addr)
{
	int err;
	udword D0;
	udword A0;
	struct DLPRPC_param argv[1];

	DLPC_TRACE(5)
		fprintf(stderr, "Inside RDLP_MemReadable(0x%08lx)\n", addr);
	D0 = A0 = 0L;

	argv[0].byref = False;
	argv[0].type = RPCP_DWord;
	argv[0].data.dword_v = addr;

	err = DlpRPC(pconn, RPCTRAP_MemReadable, &D0, &A0, 1, argv);
	DLPC_TRACE(5)
		fprintf(stderr, "RDLP_MemReadable: err == %d\n", err);
	/* XXX - Return -1 if err < 0? */
fprintf(stderr, "D0 == 0x%08lx, A0 == 0x%08lx\n", D0, A0);

	return D0;	/* Result returned in D0 */
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
