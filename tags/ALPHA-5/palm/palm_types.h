#ifndef _palm_types_h_
#define _palm_types_h_

/* Convenience types */
typedef signed   char  byte;		/* Signed 8-bit quantity */
typedef unsigned char  ubyte;		/* Unsigned 8-bit quantity */
typedef signed   short word;		/* Signed 16-bit quantity */
typedef unsigned short uword;		/* Unsigned 16-bit quantity */
typedef signed   long  dword;		/* Signed 32-bit quantity */
typedef unsigned long  udword;		/* Unsigned 32-bit quantity */

typedef udword chunkID;			/* Those IDs made up of four
					 * characters stuck together into a
					 * 32-bit quantity.
					 */
/* MAKE_CHUNKID
 * A convenience macro to make a chunkID out of four characters.
 */
#define MAKE_CHUNKID(a,b,c,d) \
	(((a) << 24) | \
	 ((b) << 16) | \
	 ((c) << 8)  | \
	 (d))

/* XXX - There ought to be something to make sure that the sizes and
 * signedness above are true.
 */

typedef enum { false = 0, true = 1 } bool;

#endif	/* _palm_types_h_ */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
