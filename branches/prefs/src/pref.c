/* pref.c
 *
 * Functions for creating the preference cache, retrieving the preferences
 * from the palm and writing the headers to the conduits.
 *
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>			/* For malloc and free */

#if HAVE_LIBINTL_H
#  include <libintl.h>			/* For i18n */
#endif	/* HAVE_LIBINTL_H */

#include "pref.h"

extern struct pref_item *pref_cache;

struct pref_item *
FindPrefItem(struct pref_desc description,
	struct pref_item *list);

struct pref_item *
GetPrefItem(struct pref_desc description);

void FreePrefItem(struct pref_item *prefitem);

/* Creates the pref_cache from a given conduit block */
int
CacheFromConduits(conduit_block *conduits, struct PConnection *pconn)
{
	conduit_block *conduit_cursor;
	pref_item *pref_cursor;
	pref_item *item;
	int i;

	/* Create a placeholder for first item */
	if((pref_cache = (pref_item *)malloc(sizeof *pref_cache))
		== NULL)
	    return -1; /* Failiure */

	pref_cursor = pref_cache;
	pref_cursor->next = NULL;
	pref_cursor->contents = NULL;
	pref_cursor->contents_info = NULL;
	pref_cursor->pconn = pconn;	/* Needed for run_conduit */

	for(conduit_cursor = conduits;
	    conduit_cursor != NULL;
	    conduit_cursor = conduit_cursor->next)
	{
		if(conduit_cursor->path == NULL)
		    continue;

		for(i = 0;
		    i < conduit_cursor->num_prefs;
		    i++)
		{
			if((item = FindPrefItem(conduit_cursor->prefs[i],
						pref_cache)) == NULL)
			{
/*				MISC_TRACE(2)*/
					fprintf(stderr,
						"Request for preference added "
						"in cache: '%c%c%c%c' %u\n",
						(char) (conduit_cursor->prefs[i].creator >> 24) & 0xff,
						(char) (conduit_cursor->prefs[i].creator >> 16) & 0xff,
						(char) (conduit_cursor->prefs[i].creator >> 8) & 0xff,
						(char) conduit_cursor->prefs[i].creator & 0xff,
						conduit_cursor->prefs[i].id);
				item = (pref_item *)malloc(sizeof *item);
				pref_cursor->next = item;
				item->next = NULL;
				item->contents = NULL;
				item->contents_info = NULL;
				item->description = conduit_cursor->prefs[i];
				item->pconn = pconn;
				pref_cursor = item;
			}
		}
	}

	pref_cursor = pref_cache->next;
	FreePrefItem(pref_cache);
	pref_cache = pref_cursor;

	return 0;	/* Success */
}

/* Makes sure the pref_item is filled with the contents found in the specified
 * database (specified in the flags of the description structure of the
 * pref_item). If none is specified, looks in Saved first and if not found,
 * looks in Unsaved. If still not found, assumes to be empty. If found in
 * either one, the flags are changed to reflect where the item was actually
 * found. Returns 0 upon success.
 */
int
FetchPrefItem(struct PConnection *pconn,
	struct pref_item *prefitem)
{
	ubyte flags;
	int err;

	if (prefitem == NULL)
	    return -1;	/* Failiure, item doesn't exist */

	if(prefitem->contents_info != NULL)
		return 0;	/* Trivial success */

	/* We save the set flags, because we are going to change the flags
	 * in the description to where we found the preference. If an error
	 * occurs, or we don't find the preference, restore flags to the set
	 * value.
	 */
	flags = prefitem->description.flags;

	/* Either we must look in the saved preferences (whatever the unsaved
	 * bit says), or the unsaved bit is not set (in which case it doesn't
	 * matter what the saved bit says, since we must look inside the saved
	 * database anyway.
	 */
	if((flags & PREFDFL_SAVED) != 0 ||
	   (flags & PREFDFL_UNSAVED) == 0)
	{
/*		MISC_TRACE(2)*/
			fprintf(stderr,"Downloading preference from Saved "
				"Preferences: '%c%c%c%c' %u\n",
				(char) (prefitem->description.creator >> 24) & 0xff,
				(char) (prefitem->description.creator >> 16) & 0xff,
				(char) (prefitem->description.creator >> 8) & 0xff,
				(char) prefitem->description.creator & 0xff,
				prefitem->description.id);
		prefitem->description.flags = PREFDFL_SAVED;
		if((err = DownloadPrefItem(pconn,prefitem)) < 0)
		{
			prefitem->description.flags = flags;
			return err;
		}
	}

	/* Did we find it? */
	if(prefitem->contents_info->version != 0)
	    return 0;

	/* Same reasoning as above */
	if((flags & PREFDFL_UNSAVED) != 0 ||
	   (flags & PREFDFL_SAVED) == 0)
	{
/*		MISC_TRACE(2)*/
			fprintf(stderr,"Downloading preference from Unsaved "
				"Preferences: '%c%c%c%c' %u\n",
				(char) (prefitem->description.creator >> 24) & 0xff,
				(char) (prefitem->description.creator >> 16) & 0xff,
				(char) (prefitem->description.creator >> 8) & 0xff,
				(char) prefitem->description.creator & 0xff,
				prefitem->description.id);
		prefitem->description.flags = PREFDFL_SAVED;
		prefitem->description.flags = PREFDFL_UNSAVED;
		if((err = DownloadPrefItem(pconn,prefitem)) < 0)
		{
			prefitem->description.flags = flags;
			return err;
		}
	}

	/* If we didn't find it, restore the flags */
	if(prefitem->contents == NULL)
		prefitem->description.flags = flags;

	return 0;	/* Success */
}

/* Downloads the preference item from the palm. Checks where to download from.
 * This routine shouldn't be called directly. Instead, call FetchPrefItem,
 * which determines where to look for the preference first.
 * The only thing that makes sense to download the prefs with just the right
 * memory, is to first download the preference into an array NULL with length
 * 0 (duh), so that DlpReadAppPreference can return the size of the preference.
 * Then we allocate the right amount of space and call it again, this time
 * with the exact size and an array that makes sense. Slower? Maybe, but it's
 * the only way to be sure you download all and not segfault.
 */
int
DownloadPrefItem(struct PConnection *pconn,
	struct pref_item *prefitem)
{
	struct dlp_apppref *contents_info;
	ubyte *contents;
	ubyte flags;
	int err;

	if((prefitem->description.flags & PREFDFL_SAVED) != 0)
		flags = PREF_SAVED;
	else
		flags = PREF_UNSAVED;

	if(prefitem->contents_info != NULL)
	    contents_info = prefitem->contents_info;
	else
	if((contents_info = malloc(sizeof *contents_info)) == NULL)
	{
		fprintf(stderr,_("%s: Out of memory.\n"),"DownloadPrefItem");
		return -1;
	}

	/* Now reset the info */
	contents_info->len = 0;		/* Length of NULL */
	contents_info->size = 0;
	contents_info->version = 0;

	/* First read. Let the routine write the real size into contents_info.
	 */
	err = DlpReadAppPreference(pconn,
				   prefitem->description.creator,
				   prefitem->description.id,
				   contents_info->len,
				   flags,
				   contents_info,
				   NULL);

	/* Check for any errors */
	if(err < 0)
	{
		/* If there was no error, contents_info doesn't exist,
		 * because the preference was simply not found.
		 */
		free(contents_info);
		return err;
	}
/*	MISC_TRACE(4)*/
		fprintf(stderr,"Preference item '%c%c%c%c' %u has a size of "
			"%u bytes\n",
			(char) (prefitem->description.creator >> 24) & 0xff,
			(char) (prefitem->description.creator >> 16) & 0xff,
			(char) (prefitem->description.creator >> 8) & 0xff,
			(char) prefitem->description.creator & 0xff,
			prefitem->description.id,
			contents_info->size);

	/* Zero size? This means either the preference was not found (so the
	 * size is 0 since we set it explicitly a few lines back), or it was
	 * found but just had size 0. Either way, don't waste any more time and
	 * just return.
	 */
	if(contents_info->size == 0)
	{
	    prefitem->contents_info = contents_info;
	    if(prefitem->contents != NULL)
		free(prefitem->contents);
	    prefitem->contents = NULL;
	    return 0;
	}

	/* Now we allocate the right amount of space */
	if((contents = calloc(sizeof *contents,contents_info->size)) == NULL)
	{
	    fprintf(stderr, _("%s: Out of memory.\n"),"DownloadPrefItem");
	    free(contents_info);
	    return -1;
	}

	/* And here we go for the second and final run. */
	err = DlpReadAppPreference(pconn,
				   prefitem->description.creator,
				   prefitem->description.id,
				   contents_info->size,
				   flags,
				   contents_info,
				   contents);

	if(err < 0)
	{
	    free(contents_info);
	    free(contents);
	    return err;
	}

/*	MISC_TRACE(3)*/
		fprintf(stderr,"Successfully downloaded %u of %u bytes of "
			"preference item '%c%c%c%c' %u\n",
			contents_info->len,
			contents_info->size,
			(char) (prefitem->description.creator >> 24) & 0xff,
			(char) (prefitem->description.creator >> 16) & 0xff,
			(char) (prefitem->description.creator >> 8) & 0xff,
			(char) prefitem->description.creator & 0xff,
			prefitem->description.id);

	/* Be nice and give the man any memory back */
	if(prefitem->contents != NULL)
	{
	    free(prefitem->contents);
	    prefitem->contents = NULL;
	 }

	prefitem->contents = contents;
	prefitem->contents_info = contents_info;

	return 0;	/* Success */
}

/* Finds a preference item from a list by matching it to the description.
 * Returns the found preference item if found, else returns NULL.
 */
struct pref_item *
FindPrefItem(struct pref_desc description,
	struct pref_item *list)
{
    struct pref_item *match = list, *previous = NULL;

    while(match != NULL)
    {
	if(prefdesccmp(match->description,description) == 0)
	    break;
	previous = match;
	match = match->next;
    }

    return match;
}


/* Returns a fully filled preference item or, if not found, returns NULL. */
struct pref_item *
GetPrefItem(struct pref_desc description)
{
    struct pref_item  *retval;

    if((retval = FindPrefItem(description,pref_cache)) == NULL)
	return NULL;

    if(FetchPrefItem(retval->pconn,retval) < 0)
	return NULL;

    return retval;
}

void
FreePrefItem(struct pref_item *prefitem)
{
    if(prefitem == NULL)
	return;

    if(prefitem->contents_info != NULL)
	free(prefitem->contents_info);

    if(prefitem->contents != NULL)
	free(prefitem->contents);

    free(prefitem);
}


/* FreePrefList
 * Iterates over all items in the list and frees them. Primary use: Destruction
 * of the cache list.
 */
void
FreePrefList(struct pref_item *list)
{
    struct pref_item *cursor;

    for(cursor = list;
	cursor != NULL;
	cursor = list)
    {
	list = cursor->next;
	FreePrefItem(cursor);
    }

    return;
}

/* Sorry, I use vim, so don't know about the Emacs stuff at the end of your
 * files
 */
