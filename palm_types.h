#ifndef _palm_types_h_
#define _palm_types_h_

#include <sys/types.h>
	/* XXX - Make sure the various [u_]int{8,16,32,64}_t types are
	 * defined
	 */

/* Convenience types */
typedef int8_t byte;
typedef u_int8_t ubyte;
typedef int16_t word;
typedef u_int16_t uword;
typedef int32_t dword;
typedef u_int32_t udword;

#endif	/* _palm_types_h_ */
