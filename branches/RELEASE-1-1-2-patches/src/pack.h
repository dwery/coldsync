/* pack.h
 *
 * Declarations for ppack() and punpack().
 *
 *	Copyright (C) 1999, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: pack.h,v 2.3 1999-08-26 14:19:44 arensb Exp $
 */
#ifndef _pack_h_
#define _pack_h_

#define PACK_END	0		/* Last argument in list */
#define PACK_UBYTE	1
#define PACK_UBYTES(n)	(PACK_UBYTE | 0x80), (n)
#define PACK_UWORD	2
#define PACK_UWORDS(n)	(PACK_UWORD | 0x80), (n)
#define PACK_UDWORD	3
#define PACK_UDWORDS(n)	(PACK_UDWORD | 0x80), (n)
#define PACK_TIME	4
#define PACK_TIMES(n)	(PACK_TIME | 0x80), (n)

extern int ppack(ubyte *buf, ...);
extern int punpack(const ubyte *buf, ...);

#endif	/* _pack_h_ */
