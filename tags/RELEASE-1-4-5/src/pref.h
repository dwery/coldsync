/* conduit.h
 *
 * Definitions, declarations pertaining to the retrieval of preferences.
 * 
 *	Copyright (C) 2000, Sumant S.R. Oemrawsingh.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: pref.h,v 2.1 2000-09-03 05:07:02 arensb Exp $
 */
#ifndef _pref_h_
#define _pref_h_

#define PREF_SAVED	0x80	/* In the saved database */
#define PREF_UNSAVED	0x00	/* In the unsaved database */

#include "config.h"
#include <coldsync.h>

/* struct pref_item
 * Typically used for the cache. This contains an entire preference.
 */
typedef struct pref_item
{
    struct pref_item	*next;	/* Next item in the list. NULL is the end */

    struct pref_desc	description;	/* Desc of this preference */

    ubyte	*contents;	/* The data retrieved from the palm. If NULL,
				 * then either it's not downloaded yet or it
				 * doesn't exist.
				 */
    struct dlp_apppref	*contents_info;	/* Information about contents. For the
					 * length of contents, len should be
					 * used.
					 */

    /* XXX - This field seems awfully bogus. Try taking it out and see what
     * happens.
     */
    struct PConnection *pconn;	/* The connection from which to get these
				 * preferences. This isn't very elegant, but
				 * it beats having to provide the run_conduit
				 * routine with pconn. Is there a cleaner way
				 * to do this?
				 */
} pref_item;

extern int CacheFromConduits(const conduit_block *conduits,
			     struct PConnection *pconn);
extern int FetchPrefItem(struct PConnection *pconn,
			 pref_item *prefitem);
extern int DownloadPrefItem(struct PConnection *pconn,
			    pref_item *prefitem);
/* XXX - 'description' in the next few functions, is a struct that's passed
 * by value. Yuck! See if 'const struct pref_desc *description' will work.
 */
extern struct pref_item *FindPrefItem(
	const struct pref_desc *description,
	struct pref_item *list);
extern struct pref_item *GetPrefItem(
	struct pref_desc *description);
extern void FreePrefItem(struct pref_item *prefitem);
extern void FreePrefList(struct pref_item *list);

#endif	/* _pref_h_ */

/* This is for Emacs's benefit:
 * Local Variables:	***
 * fill-column:	75	***
 * End:			***
 */
