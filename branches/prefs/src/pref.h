/* conduit.h
 *
 * Definitions, declarations pertaining to the retrieval of preferences.
 * 
 */
#ifndef _pref_h_
#define _pref_h_

#define PREF_SAVED	0x80	/* In the saved database */
#define PREF_UNSAVED	0x00	/* In the unsaved database */

#include <coldsync.h>

#define prefdesccmp(x,y)	(((x).creator == (y).creator && (x).id == (y).id) ? 0 : 1)

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

    struct PConnection *pconn;	/* The connection from which to get these
				 * preferences. This isn't very elegant, but
				 * it beats having to provide the run_conduit
				 * routine with pconn. Is there a cleaner way
				 * to do this?
				 */
} pref_item;

int CacheFromConduits(conduit_block *conduits,struct PConnection *pconn);

int FetchPrefItem(struct PConnection *pconn, pref_item *prefitem);

int DownloadPrefItem(struct PConnection *pconn, pref_item *prefitem);

struct pref_item *
FindPrefItem(struct pref_desc description,
	struct pref_item *list);

struct pref_item *
GetPrefItem(struct pref_desc description);

void
FreePrefItem(struct pref_item *prefitem);

void
FreePrefList(struct pref_item *list);

#endif	/* _pref_h_ */

/* Sorry, I use vim, so don't know about the Emacs stuff at the end of your
 * files
 */
