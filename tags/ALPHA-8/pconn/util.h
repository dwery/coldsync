/* util.h
 *
 * Misc. useful stuff.
 *
 * $Id: util.h,v 1.3 1999-06-24 02:48:09 arensb Exp $
 */
#ifndef _util_h_
#define _util_h_

#include <time.h>
#include "palm/palm_types.h"
#include "pconn/dlp_cmd.h"

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

/* Functions for converting between DLP's time format and Unix's
 * time_ts and the time_t-with-offset that the rest of the Palm stuff
 * uses.
 */
extern time_t time_dlp2time_t(const struct dlp_time *dlpt);
extern udword time_dlp2palmtime(const struct dlp_time *dlpt);
extern void time_time_t2dlp(const time_t t, struct dlp_time *dlpt);
extern void time_palmtime2dlp(const udword palmt, struct dlp_time *dlpt);

extern void debug_dump(FILE *outfile, const char *prefix,
		       const ubyte *buf, const udword len);

extern uword crc16(const ubyte *buf, const uword len, const uword start);

#endif	/* _util_h_ */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
