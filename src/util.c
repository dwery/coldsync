/* util.c
 *
 * Misc. utility functions.
 *
 * The get_*() functions are used to extract values out of strings of
 * ubytes and convert them to the native format.
 * The put_*() functions, conversely, are used to take a value in the
 * native format, convert them to Palm (big-endian) format, and write
 * them to a ubyte string.
 *
 * $Id: util.c,v 1.5 1999-08-25 08:20:27 arensb Exp $
 */

#include "config.h"
#include <stdio.h>
#include <ctype.h>	/* For isprint() */
#include "util.h"
#include "pdb.h"	/* For EPOCH_1904 */

INLINE ubyte
peek_ubyte(const ubyte *buf)
{
	return buf[0];
}

INLINE uword
peek_uword(const ubyte *buf)
{
	return ((uword) buf[0] << 8) |
		buf[1];
}

INLINE udword
peek_udword(const ubyte *buf)
{
	return ((uword) buf[0] << 24) |
		((uword) buf[1] << 16) |
		((uword) buf[2] << 8) |
		buf[3];
}

INLINE ubyte
get_ubyte(const ubyte **buf)
{
	ubyte retval;

	retval = peek_ubyte(*buf);
	*buf += sizeof(ubyte);

	return retval;
}

INLINE void
put_ubyte(ubyte **buf, ubyte value)
{
	**buf = value;
	++(*buf);
}

INLINE uword
get_uword(const ubyte **buf)
{
	uword retval;

	retval = peek_uword(*buf);
	*buf += sizeof(uword);

	return retval;
}

INLINE void
put_uword(ubyte **buf, uword value)
{
	**buf = (value >> 8) & 0xff;
	++(*buf);
	**buf = value & 0xff;
	++(*buf);
}

INLINE udword
get_udword(const ubyte **buf)
{
	udword retval;

	retval = peek_udword(*buf);
	*buf += sizeof(udword);

	return retval;
}

INLINE void
put_udword(ubyte **buf, udword value)
{
	**buf = (value >> 24) & 0xff;
	++(*buf);
	**buf = (value >> 16) & 0xff;
	++(*buf);
	**buf = (value >>  8) & 0xff;
	++(*buf);
	**buf = value & 0xff;
	++(*buf);
}

/* XXX - Figure out the timezone hairiness:
 * Palms don't have timezones. Hence, the Palm's epoch is Jan. 1, 1904 in
 * the local timezone.
 * Unless you're syncing across the network, in which case its epoch is
 * Jan. 1, 1904 in the timezone it happens to be in (which may not be the
 * same as the desktop's timezone).
 * Except that there are (I'm sure) tools that add timezones to the Palm.
 * These should be consulted.
 * Times generated locally are in the local timezone (i.e., the one that
 * the desktop machine is in).
 */

/* time_dlp2time_t
 * Convert the DLP time structure into a Unix time_t, and return it.
 */
time_t
time_dlp2time_t(const struct dlp_time *dlpt)
{
	struct tm tm;

	/* Convert the dlp_time into a struct tm, then just use mktime() to
	 * do the conversion.
	 */
	tm.tm_sec = dlpt->second;
	tm.tm_min = dlpt->minute;
	tm.tm_hour = dlpt->hour;
	tm.tm_mday = dlpt->day;
	tm.tm_mon = dlpt->month - 1;
	tm.tm_year = dlpt->year - 1900;
	tm.tm_wday = 0;
	tm.tm_yday = 0;
	tm.tm_isdst = 0;
#if HAVE_TM_ZONE
	tm.tm_gmtoff = 0;
	tm.tm_zone = NULL;
#else
/* XXX - ANSI doesn't allow #warning, and we're not using the timezone for
 * anything yet.
 */
/*  #warning You do not have tm_zone */
#endif

	return mktime(&tm);
}

/* time_dlp2palmtime
 * Convert a DLP time structure into a Palm time_t (number of seconds since
 * Jan. 1. 1904), and return it.
 */
udword
time_dlp2palmtime(const struct dlp_time *dlpt)
{
	time_t now;		/* The time, as a Unix time_t */
	struct tm tm;

	/* Convert the dlp_time into a struct tm, use mktime() to do the
	 * conversion, and add the difference in epochs.
	 */
	tm.tm_sec = dlpt->second;
	tm.tm_min = dlpt->minute;
	tm.tm_hour = dlpt->hour;
	tm.tm_mday = dlpt->day;
	tm.tm_mon = dlpt->month - 1;
	tm.tm_year = dlpt->year - 1900;
	tm.tm_wday = 0;
	tm.tm_yday = 0;
	tm.tm_isdst = 0;
#if HAVE_TM_ZONE
	tm.tm_gmtoff = 0;
	tm.tm_zone = NULL;
#endif

	now = mktime(&tm);
	now += EPOCH_1904;

	return now;
}

/* time_time_t2dlp
 * Convert a Unix time_t into a DLP time structure. Put the result in
 * 'dlpt'.
 */
void
time_time_t2dlp(const time_t t,
		struct dlp_time *dlpt)
{
	struct tm *tm;

	tm = localtime(&t);	/* Break 't' down into components */

	/* Copy the relevant fields over to 'dlpt' */
	dlpt->year = tm->tm_year + 1900;
	dlpt->month = tm->tm_mon + 1;
	dlpt->day = tm->tm_mday;
	dlpt->hour = tm->tm_hour;
	dlpt->minute = tm->tm_min;
	dlpt->second = tm->tm_sec;
}

/* time_palmtime2dlp

 * Convert a Palm time (seconds since the Jan. 1, 1904) to a DLP time
 * structure. Put the result in 'dlpt'.
 */
void
time_palmtime2dlp(const udword palmt,
		  struct dlp_time *dlpt)
{
	struct tm *tm;
	time_t t;

	/* Convert the Palm time to a Unix time_t */
	t = palmt - EPOCH_1904;

	/* Break the Unix time_t into components */
	tm = localtime(&t);

	/* Copy the relevant fields over to 'dlpt' */
	dlpt->year = tm->tm_year + 1900;
	dlpt->month = tm->tm_mon + 1;
	dlpt->day = tm->tm_mday;
	dlpt->hour = tm->tm_hour;
	dlpt->minute = tm->tm_min;
	dlpt->second = tm->tm_sec;
}

/* debug_dump
 * Dump the contents of an array of ubytes to stderr, for debugging.
 */
void
debug_dump(FILE *outfile, const char *prefix,
	   const ubyte *buf, const udword len)
{
	int lineoff;

	for (lineoff = 0; lineoff < len; lineoff += 16)
	{
		int i;

		fprintf(outfile, "%s ", prefix);
		for (i = 0; i < 16; i++)
		{
			if (lineoff + i < len)
			{
				/* Regular bytes */
				fprintf(outfile, "%02x ", buf[lineoff+i]);
			} else {
				/* Filler at the end of the line */
				fprintf(outfile, "   ");
			}
		}
		fprintf(outfile, "  | ");
		for (i = 0; i < 16; i++)
		{
			if (lineoff + i < len)
			{
				/* Regular bytes */
				if (isprint(buf[lineoff+i]))
					fprintf(outfile, "%c", buf[lineoff+i]);
				else
					fprintf(outfile, ".");
			} else
				break;
		}
		fprintf(outfile, "\n");
	}
}

/* icrctb
 * Table of CRC input values for crc16() (qv).
 */
static uword icrctb[256] = {
	0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
	0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
	0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
	0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
	0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
	0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
	0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
	0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
	0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
	0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
	0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
	0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
	0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
	0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
	0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
	0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
	0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
	0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
	0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
	0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
	0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
	0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
	0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
	0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
	0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
	0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
	0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
	0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
	0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
	0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
	0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
	0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0,
};

/* crc16
 * Calculate a 16-bit CRC for the 'len' bytes of data in 'buf'.
 * 'start' is an initial value for the CRC; this allows us to
 * calculate the total CRC of a set of several buffers.
 *
 * This function is essentially icrc() from the Numerical Recipes[1],
 * but trimmed down to just the parts that the Palm CRC function uses,
 * and with the CRC table already precomputed ('icrctb', above).
 * If you want to derive this independently, you'll probably want to
 * know that crc16() uses the CCITT polynomial (0x1021), jinit = -1
 * and jrev = 0.
 *
 * [1] W. Press, S. Teukolsky et al., "Numerical Recipes in C: the Art
 * of Scientific Computing", 2nd ed. Cambridge University Press, 1992.
 */
uword
crc16(const ubyte *buf,
     uword len,
     uword start)
{
	uword crc = start;

	for (; len > 0; len--)
		crc = icrctb[*buf++ ^ (crc >> 8)] ^ ((crc & 0xff) << 8);
	return crc;
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
