/* coldsync.h
 *
 * Data structures and such needed by 'coldsync'.
 *
 * $Id: coldsync.h,v 1.1 1999-02-21 08:56:35 arensb Exp $
 */
#ifndef _coldsync_h_
#define _coldsync_h_

/* ColdPalm
 * Information about the Palm being currently synced.
 */
struct ColdPalm
{
	struct PConnection *pconn;	/* Connection to the Palm */
	int num_cards;			/* # memory cards */
};

#endif	/* _coldsync_h_ */
