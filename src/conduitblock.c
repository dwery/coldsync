/* conduitblock.c
 *
 * Functions dealing with conduit blocks.
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	Copyright (C) 2002, Alessandro Zummo
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: conduitblock.c,v 2.3 2002-10-16 18:59:32 azummo Exp $
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>

#if HAVE_LIBINTL_H
#  include <libintl.h>		/* For i18n */
#endif  /* HAVE_LIBINTL_H */

#include "coldsync.h"
#include "parser.h"		/* For config file parser stuff */


/* new_conduit_block
 * Allocate and initialize a new conduit_block.
 */
conduit_block *
new_conduit_block()
{
	conduit_block *retval;

	/* Allocate the new conduit_block */
	if ((retval = (conduit_block *) malloc(sizeof(conduit_block))) == NULL)
		return NULL;

	/* Initialize the new conduit_block */
	retval->next		= NULL;
	retval->flavors		= 0;
	retval->ctypes		= NULL;
	retval->ctypes_slots	= 0;
	retval->num_ctypes	= 0;
	retval->flags		= 0;
	retval->path		= NULL;
	retval->cwd		= NULL;
	retval->headers		= NULL;
	retval->prefs		= NULL;
	retval->prefs_slots	= 0;
	retval->num_prefs	= 0;

	return retval;
}

/* free_conduit_block
 * Free a conduit block. Note that this function does not pay attention to
 * any other conduit_blocks on the list. If you have a list of
 * conduit_blocks, you can use this function to free each individual element
 * in the list, but not the whole list.
 */
void
free_conduit_block(conduit_block *c)
{
	struct cond_header *hdr;
	struct cond_header *next_hdr;

	if (c->path != NULL)
		free(c->path);

	if (c->cwd != NULL)
		free(c->cwd);

	/* Free the conduit headers */
	/* XXX Is next_hdr = NULL necessary? */
	for (hdr = c->headers, next_hdr = NULL; hdr != NULL; hdr = next_hdr)
	{
		next_hdr = hdr->next;
		if (hdr->name != NULL)
			free(hdr->name);
		if (hdr->value != NULL)
			free(hdr->value);
		free(hdr);
	}

	/* Free the 'ctypes' array */
	if (c->ctypes != NULL)
		free(c->ctypes);

	/* Free the 'pref_desc' array */
	if (c->prefs != NULL)
		free(c->prefs);

	free(c);
}

/* append_pref_desc
 * Appends a preference descriptor to the conduit_block 'cond'.
 * Returns 0 if successful, a negative value otherwise.
 */
int
append_pref_desc(conduit_block *cond,	/* Conduit block to add to */
		 const udword creator,	/* Preference creator */
		 const uword id,	/* Preference identifier */
		 const char flags)	/* Preference flags (see PREFDFL_*) */
{
	/* Is this the first preference being added? */
	if (cond->prefs == NULL)
	{
		/* Yes. Start by allocating size for 4 entries, for
		 * starters.
		 */
		MISC_TRACE(7)
			fprintf(stderr, "Allocating a new 'prefs' array.\n");
		if ((cond->prefs = (struct pref_desc *)
		     calloc(4, sizeof(struct pref_desc))) == NULL)
		{
			/* Can't allocate new array */
			return -1;
		}
		cond->prefs_slots = 4;

	} else if (cond->num_prefs >= cond->prefs_slots)
	{
		struct pref_desc *newprefs;

		/* This is not the first preference, but the 'prefs' array
		 * is full and needs to be extended. Double its length.
		 */
		MISC_TRACE(7)
			fprintf(stderr, "Extending prefs array to %d\n",
				cond->prefs_slots * 2);
		if ((newprefs = (struct pref_desc *)
		     realloc(cond->prefs, 2 * cond->prefs_slots *
			     sizeof(struct pref_desc))) == NULL)
		{
			/* Can't extend array */
			return -1;
		}
		cond->prefs = newprefs;
		cond->prefs_slots *= 2;
	}

	/* If we get this far, then cond->prefs is long enough to hold the
	 * new preference descriptor. Add it.
	 */
	cond->prefs[cond->num_prefs].id		= id;
	cond->prefs[cond->num_prefs].flags	= flags;
	cond->prefs[cond->num_prefs].creator	= creator;
	cond->num_prefs++;

	return 0;		/* Success */
}

/* append_crea_type
 * Appends a creator/type pair to the conduit_block 'cond'.
 * Returns 0 if successful, a negative value otherwise.
 */
int
append_crea_type(conduit_block *cond,	/* Conduit block to add to */
		 const udword creator,	/* Database creator */
		 const udword type,	/* Database type */
		 const unsigned char flags)
{
	/* Is this the first creator/type pair being added? */
	if (cond->ctypes == NULL)
	{
		/* Yes. Start by allocating size for 1 entry, for
		 * starters.
		 * (In most memory-allocation schemes of this type, one
		 * allocates room for more than one element, but in this
		 * case, the vast majority of conduits will only have one
		 * element.)
		 */
		MISC_TRACE(7)
			fprintf(stderr, "Allocating a new 'ctypes' array.\n");
		if ((cond->ctypes = (crea_type_t *)
		     calloc(4, sizeof(crea_type_t))) == NULL)
		{
			/* Can't allocate new array */
			return -1;
		}
		cond->ctypes_slots = 4;

	} else if (cond->num_ctypes >= cond->ctypes_slots)
	{
		crea_type_t *newctypes;

		/* This is not the first creator/type pair, but the
		 * 'ctypes' array is full and needs to be extended. Double
		 * its length.
		 */
		MISC_TRACE(7)
			fprintf(stderr, "Extending ctypes array to %d\n",
				cond->ctypes_slots * 2);
		if ((newctypes = (crea_type_t *)
		     realloc(cond->ctypes, 2 * cond->ctypes_slots *
			     sizeof(crea_type_t))) == NULL)
		{
			/* Can't extend array */
			return -1;
		}
		cond->ctypes = newctypes;
		cond->ctypes_slots *= 2;
	}

	/* If we get this far, then cond->ctypes is long enough to hold the
	 * new creator/type pair. Add it.
	 */
	cond->ctypes[cond->num_ctypes].type	= type;
	cond->ctypes[cond->num_ctypes].flags	= flags;
	cond->ctypes[cond->num_ctypes].creator	= creator;
	cond->num_ctypes++;

	return 0;		/* Success */
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
