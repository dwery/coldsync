/* util.h
 *
 * Misc. useful stuff.
 *
 * $Id: util.h,v 1.1 1999-02-19 22:51:55 arensb Exp $
 */
#ifndef _util_h_
#define _util_h_

#include "palm/palm_types.h"

#ifdef __GNUC__
#  define INLINE __inline__
#else
   /* XXX - Really should have a HAVE_INLINE configuration option */
#  define INLINE
#endif	/* __GNUC__ */

/* Functions for reading a value from an array of ubytes */
extern INLINE ubyte peek_ubyte(const ubyte *buf);
extern INLINE uword peek_uword(const ubyte *buf);
extern INLINE udword peek_udword(const ubyte *buf);

/* Functions for extracting values from an array of ubytes */
extern INLINE ubyte get_ubyte(const ubyte **buf);
extern INLINE uword get_uword(const ubyte **buf);
extern INLINE udword get_udword(const ubyte **buf);

/* Functions for writing values to an array of ubytes */
extern INLINE void put_ubyte(ubyte **buf, const ubyte value);
extern INLINE void put_uword(ubyte **buf, const uword value);
extern INLINE void put_udword(ubyte **buf, const udword value);

extern void debug_dump(FILE *outfile, const char *prefix,
		       const ubyte *buf, const udword len);

extern uword crc16(const ubyte *buf, const uword len, const uword start);

#endif	/* _util_h_ */
