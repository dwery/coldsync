/* pack.c
 *
 * Defines the ppack() and punpack() functions, which facilitate
 * reading and writing data structures.
 *
 * $Id: pack.c,v 2.2 1999-08-25 04:06:21 arensb Exp $
 */

#include <stdio.h>
#include <stdarg.h>	/* For va_* */
#include "palm_types.h"
#include "util.h"
#include "pack.h"

/* ppack
 * Write a number of values to 'buf', in Palm (big-endian) byte order.
 * This facilitates writing a data structure to a buffer to send it to
 * the Palm. For example:
 *
 *	ubyte buf[128];
 *	ubyte b;
 *	uword w;
 *	udword dw;
 *
 *	ppack(buf,
 *	      PACK_UBYTE,	b,
 *	      PACK_UWORD,	w,
 *	      PACK_UDWORD,	dw,
 *	      PACK_END);
 *
 * This example writes 7 bytes to 'buf': an unsigned byte (whose value
 * is given by 'b'), an unsigned word (whose value is given by 'w'),
 * and an unsigned doubleword (whose value is given by 'dw').
 *	Values are written in the order in which they appear in the
 * argument list. The ppack() function does not perform any alignment;
 * this must be done by the caller. The ppack() also does not know the
 * size of 'buf', so the caller is responsible for making sure that it
 * does not overflow.
 *	ppack() can also write arrays of values, rather than single
 * values:
 *
 *	ubyte buf[128];
 *	ubyte ba[20];
 *
 *	ppack(buf,
 *	      PACK_UBYTES(10),	ba,
 *	      PACK_END);
 *
 * will read the first 10 values from 'ba' and write them, in order,
 * to 'buf'. Similar macros, "PACK_UWORDS()" and "PACK_UDWORDS()"
 * exist for arrays of words and doublewords.
 *	ppack() returns the number of bytes successfully written, or
 *  -1 in case of error.
 */
int
ppack(ubyte *buf,
      ...)
{
	int retval = 0;
	va_list ap;

	va_start(ap, buf);	/* Make 'ap' point to first unnamed
				 * argument.
				 */
	while (1)
	{
		int argtype;		/* Type of the next thing to put
					 * in 'buf' */

		argtype = va_arg(ap, int);
					/* Get the next argument type */
		if (argtype == PACK_END)
			/* End of argument list */
			break;

		/* If the 0x80 bit is set, then this is an array of
		 * values, rather than a single value.
		 */
		if ((argtype & 0x80) != 0)
		{
			/* It's an array */
			ubyte *ubyte_arg;
			uword *uword_arg;
			udword *udword_arg;
			struct dlp_time *time_arg;
			int arraylen;
			int i;

			argtype &= ~0x80;
			arraylen = va_arg(ap, int);

			switch (argtype)
			{
			    case PACK_UBYTE:	/* Unpack array of bytes */
				ubyte_arg = va_arg(ap, ubyte *);
				for (i = 0; i < arraylen; i++)
				{
					put_ubyte(&buf, *ubyte_arg);
					ubyte_arg++;
					retval++;
				}
				break;
			    case PACK_UWORD:	/* Unpack array of words */
				uword_arg = va_arg(ap, uword *);
				for (i = 0; i < arraylen; i++)
				{
					put_uword(&buf, *uword_arg);
					uword_arg++;
					retval++;
				}
				break;
			    case PACK_UDWORD:	/* Unpack array of
						 * doublewords */
				udword_arg = va_arg(ap, udword *);
				for (i = 0; i < arraylen; i++)
				{
					put_udword(&buf, *udword_arg);
					udword_arg++;
					retval++;
				}
				break;
			    case PACK_TIME:	/* Unpack Palm time 
						 * struct */
				time_arg = va_arg(ap, struct dlp_time *);
				for (i = 0; i < arraylen; i++)
				{
					put_uword(&buf, time_arg[i].year);
					put_ubyte(&buf, time_arg[i].month);
					put_ubyte(&buf, time_arg[i].day);
					put_ubyte(&buf, time_arg[i].hour);
					put_ubyte(&buf, time_arg[i].minute);
					put_ubyte(&buf, time_arg[i].second);
					put_ubyte(&buf, 0);	/* Padding */
					retval += 8;
				}
				break;
			    default:
				fprintf(stderr,
					"ppack: Unknown argument type %d\n",
					argtype | 0x80);
				return -1;
			}
		} else {
			/* It's a single value */
			ubyte ubyte_arg;
			uword uword_arg;
			udword udword_arg;
			struct dlp_time *time_arg;

			switch (argtype)
			{
			    case PACK_UBYTE:	/* Pack unsigned byte */
				ubyte_arg = va_arg(ap, ubyte);
				put_ubyte(&buf, ubyte_arg);
				retval++;
				break;
			    case PACK_UWORD:	/* Pack unsigned word */
				uword_arg = va_arg(ap, uword);
				put_uword(&buf, uword_arg);
				retval++;
				break;
			    case PACK_UDWORD:	/* Pack unsigned doubleword */
				udword_arg = va_arg(ap, udword);
				put_udword(&buf, udword_arg);
				retval++;
				break;
			    case PACK_TIME:	/* Pack Palm time type */
				time_arg = va_arg(ap, struct dlp_time *);
				put_uword(&buf, time_arg->year);
				put_ubyte(&buf, time_arg->month);
				put_ubyte(&buf, time_arg->day);
				put_ubyte(&buf, time_arg->hour);
				put_ubyte(&buf, time_arg->minute);
				put_ubyte(&buf, time_arg->second);
				put_ubyte(&buf, 0);	/* Padding */
				retval += 8;
				break;
			    default:		/* Unknown type */
				fprintf(stderr,
					"ppack: Unknown argument type %d\n",
					argtype);
				return -1;
			}
		}
	}
	va_end(ap);

	return retval;
}

/* punpack
 * punpack() is the inverse of ppack(): it simplifies unpacking a
 * stream of bytes (e.g, a buffer that was just read from the Palm).
 *	'buf' is assumed to contain a string of unsigned byte,
 * unsigned word, and/or unsigned doubleword values in Palm
 * (big-endian) order. punpack() reads each value in turn, in the
 * order specified by the arguments, and stores its value in the
 * specified variables:
 *
 *	ubyte buf[7];
 *	ubyte b;
 *	uword w;
 *	udword dw;
 *
 *	punpack(buf,
 *	        PACK_UBYTE,	&b,
 *	        PACK_UWORD,	&w,
 *	        PACK_UDWORD,	&dw,
 *	        PACK_END);
 *
 * This fragment reads an unsigned byte, an unsigned word, and an
 * unsigned doubleword from 'buf', in that order, and stores their
 * values in 'b', 'w' and 'dw' respectively.
 * 	Like ppack(), punpack() can extract arrays of values, using
 * the PACK_UBYTES(), PACK_UWORDS() and PACK_UDWORDS() macros.
 *	Like ppack(), punpack() does not take care of alignment
 * issues, and it assumes that the variables and arrays that it is
 * passed are large enough to hold the values to be read.
 *
 *	punpack() returns the number of bytes read from 'buf', or -1
 * in case of error.
 */
int
punpack(const ubyte *buf,
	...)
{
	int retval = 0;
	va_list ap;

	va_start(ap, buf);	/* Make 'ap' point to first unnamed
				 * argument.
				 */
	while (1)
	{
		int argtype;		/* Type of the next thing to put
					 * in 'buf' */

		argtype = va_arg(ap, int);
					/* Get the next argument type */
		if (argtype == PACK_END)
			/* End of argument list */
			break;

		/* If the 0x80 bit is set, then this is an array of
		 * values, rather than a single value.
		 */
		if ((argtype & 0x80) != 0)
		{
			/* It's an array of values */
			ubyte *ubyte_arg;
			uword *uword_arg;
			udword *udword_arg;
			struct dlp_time *time_arg;
			int arraylen;
			int i;

			argtype &= ~0x80;
					/* Turn off the 'array' bit */
			arraylen = va_arg(ap, int);
					/* # of values in the array */

			switch (argtype)
			{
			    case PACK_UBYTE:	/* Pack array of bytes */
				ubyte_arg = va_arg(ap, ubyte *);
				for (i = 0; i < arraylen; i++)
				{
					*ubyte_arg = get_ubyte(&buf);
					ubyte_arg++;
					retval++;
				}
				break;
			    case PACK_UWORD:	/* Pack array of words */
				uword_arg = va_arg(ap, uword *);
				for (i = 0; i < arraylen; i++)
				{
					*uword_arg = get_uword(&buf);
					uword_arg++;
					retval++;
				}
				break;
			    case PACK_UDWORD:	/* Pack array of doublewords */
				udword_arg = va_arg(ap, udword *);
				for (i = 0; i < arraylen; i++)
				{
					*udword_arg = get_udword(&buf);
					udword_arg++;
					retval++;
				}
				break;
			    case PACK_TIME:	/* Pack array of Palm time
						 * structs */
				time_arg = va_arg(ap, struct dlp_time *);
				for (i = 0; i < arraylen; i++)
				{
					time_arg->year = get_uword(&buf);
					time_arg->month = get_ubyte(&buf);
					time_arg->day = get_ubyte(&buf);
					time_arg->hour = get_ubyte(&buf);
					time_arg->minute = get_ubyte(&buf);
					time_arg->second = get_ubyte(&buf);
					get_ubyte(&buf);	/* Padding */
					time_arg++;
					retval += 8;
				}
				break;
			    default:
				fprintf(stderr,
					"punpack: Unknown argument type %d\n",
					argtype | 0x80);
				return -1;
			}
		} else {
			/* It's a single value */
			ubyte *ubyte_arg;
			uword *uword_arg;
			udword *udword_arg;
			struct dlp_time *time_arg;

			switch (argtype)
			{
			    case PACK_UBYTE:	/* Pack unsigned byte */
				ubyte_arg = va_arg(ap, ubyte *);

				*ubyte_arg = get_ubyte(&buf);
				retval++;
				break;
			    case PACK_UWORD:	/* Pack unsigned word */
				uword_arg = va_arg(ap, uword *);

				*uword_arg = get_uword(&buf);
				retval += 2;
				break;
			    case PACK_UDWORD:	/* Pack unsigned doubleword */
				udword_arg = va_arg(ap, udword *);

				*udword_arg = get_udword(&buf);
				retval += 4;
				break;
			    case PACK_TIME:	/* Pack Palm time struct */
				time_arg = va_arg(ap, struct dlp_time *);

				time_arg->year = get_uword(&buf);
				time_arg->month = get_ubyte(&buf);
				time_arg->day = get_ubyte(&buf);
				time_arg->hour = get_ubyte(&buf);
				time_arg->minute = get_ubyte(&buf);
				time_arg->second = get_ubyte(&buf);
				get_ubyte(&buf);	/* Padding */
				retval += 8;
				break;
			    default:		/* Unknown type */
				fprintf(stderr,
					"punpack: Unknown argument type %d\n",
					argtype);
				return -1;
			}
		}
	}
	va_end(ap);

	return retval;
}
