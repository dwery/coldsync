/* util.h
 *
 * Misc. useful stuff.
 *
 * $Id: util.h,v 1.1 1999-01-31 22:26:48 arensb Exp $
 */
#ifndef _util_h_
#define _util_h_

#include "palm_types.h"

#ifdef __GNUC__
#  define INLINE __inline__
#else
   /* XXX - Really should have a HAVE_INLINE configuration option */
#  define INLINE
#endif	/* __GNUC__ */

/* Functions for reading a value from an array of ubytes */
extern INLINE ubyte peek_ubyte(ubyte *buf);
extern INLINE uword peek_uword(ubyte *buf);
extern INLINE udword peek_udword(ubyte *buf);

/* Functions for extracting values from an array of ubytes */
extern INLINE ubyte get_ubyte(ubyte **buf);
extern INLINE uword get_uword(ubyte **buf);
extern INLINE udword get_udword(ubyte **buf);

/* Functions for writing values to an array of ubytes */
extern INLINE void put_ubyte(ubyte **buf, ubyte value);
extern INLINE void put_uword(ubyte **buf, uword value);
extern INLINE void put_udword(ubyte **buf, udword value);

extern void debug_dump(char *prefix, ubyte *buf, udword len);

extern uword crc16(const ubyte *buf, uword len, uword start);

#endif	/* _util_h_ */
