/* listenblock.c
 *
 * Functions dealing with listen blocks.
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	Copyright (C) 2002, Alessandro Zummo
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: listenblock.c,v 2.4 2002-11-02 12:51:18 azummo Exp $
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>

#if HAVE_LIBINTL_H
#  include <libintl.h>		/* For i18n */
#endif  /* HAVE_LIBINTL_H */

#include "coldsync.h"
#include "pconn/pconn.h"
#include "parser.h"		/* For config file parser stuff */
#include "symboltable.h"
#include "cs_error.h"



int
prepend_listen_block(char *devname, pconn_listen_t listen_type, pconn_proto_t protocol)
{
	listen_block *l;

	if ((l = new_listen_block()) == NULL)
	{
		Error(_("Can't allocate listen block."));
		return -1;
	}

	if (devname)
	{
		if ((l->device = strdup(devname)) == NULL)
		{
			Error(_("Can't copy string."));
			free_listen_block(l);
			return -1;
		}
	}

	/* The default is LISTEN_SERIAL, set by new_listen_block(). So we 
	 * should take care when changing it. 
	 */
	 	
	if (listen_type != LISTEN_NONE)
		l->listen_type = listen_type;

	l->protocol = protocol;

	/* Prepend the new listen block to the list of listen blocks */
	l->next = sync_config->listen;
	sync_config->listen = l;

	return 0;
}

/* name2listen_type
 * Convert the name of a listen type to its integer value. See the LISTEN_*
 * defines in "pconn/PConnection.h".
 */
pconn_listen_t
name2listen_type(const char *str)
{
	/* XXX - It'd be really nice if these strings were translatable   */
	/* mmm.. Microxoft translates macro names in their spreadsheet... */
	if (strcasecmp(str, "serial") == 0)
		return LISTEN_SERIAL;
	if (strcasecmp(str, "net") == 0)
		return LISTEN_NET;
	if (strcasecmp(str, "usb") == 0)
		return LISTEN_USB;
	return LISTEN_NONE;		/* None of the above */
}

/* Finds the named listen_block or gives back a default one if name == NULL */

listen_block *
find_listen_block(char *name)
{
 	if (name)
 	{
 	 	listen_block *l;
 	
 		MISC_TRACE(2)
 		        fprintf(stderr, "Searching for listen block: \"%s\"\n",
 		                name);   

		for (l = sync_config->listen; l != NULL; l = l->next)
		{
		 	if (l->name == NULL)
		 	        continue;   

			MISC_TRACE(3)
			        fprintf(stderr, " evaluating block \"%s\".\n", l->name);

			if (strcmp(l->name, name) == 0)
			{
			 	MISC_TRACE(2)
			 	        fprintf(stderr, " found.\n");
			 	return l;
			}
		}

		Error(_("Couldn't find the requested listen block: \"%s\""), name);
		return NULL;
	}

	/* Fall back */
	return sync_config->listen;
}

/* new_listen_block
 * Allocate and initialize a new listen_block.
 * Returns a pointer to the new listen_block, or NULL in case of error.
 */
listen_block *
new_listen_block()
{
	listen_block *retval;

	/* Allocate the new listen_block */
	if ((retval = (listen_block *) malloc(sizeof(listen_block))) == NULL)
		return NULL;

	/* Initialize the new listen_block */
	retval->next		= NULL;
	retval->listen_type	= LISTEN_SERIAL;	/* By default */
	retval->protocol	= PCONN_STACK_DEFAULT;
	retval->device		= NULL;
	retval->speed		= 0L;
	retval->flags		= LISTENFL_PROMPT;

	return retval;
}

/* free_listen_block
 * Free a listen block. Note that this function does not pay attention to
 * any other listen_blocks on the list. If you have a list of
 * listen_blocks, you can use this function to free each individual element
 * in the list, but not the whole list.
 */
void
free_listen_block(listen_block *l)
{
	if (l->device != NULL)
		free(l->device);
	free(l);
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
