/* pdablock.c
 *
 * Functions dealing with pda blocks.
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	Copyright (C) 2002, Alessandro Zummo.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: pdablock.c,v 2.3 2002-10-16 18:59:32 azummo Exp $
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


/* print_pda_block
 * Print to 'outfile' a suggested PDA block that would go in a user's
 * .coldsyncrc . The values in the suggested PDA block come from 'pda' and
 * 'palm'.
 */
void
print_pda_block(FILE *outfile, const pda_block *pda, struct Palm *palm)
{
	udword p_userid;
	const char *p_username;
	const char *p_snum;
	int p_snum_len;

	/* Get length of serial number */
	p_snum_len = palm_serial_len(palm);
	if (p_snum_len < 0)
		return;		/* Something went wrong */

	/* Get serial number */
	p_snum = palm_serial(palm);
	if ((p_snum == NULL) && !palm_ok(palm))
		return;

	/* Get userid */
	p_userid = palm_userid(palm);
	if ((p_userid == 0) && !palm_ok(palm))
		return;		/* Something went wrong */

	/* Get username */
	p_username = palm_username(palm);
	if ((p_username == NULL) && !palm_ok(palm))
		return;		/* Something went wrong */

	/* First line of PDA block */
	if ((pda == NULL) || (pda->name == NULL) ||
	    (pda->name[0] == '\0'))
		printf("pda {\n");
	else
		printf("pda \"%s\" {\n", pda->name);

	/* "snum:" line in PDA block */
	if (p_snum == NULL)
		;	/* Omit the "snum:" line entirely */
	else if (p_snum[0] == '\0')
	{
		/* This Palm doesn't have a serial number. Say so. */
		printf("\tsnum: \"\";\n");
	} else if (p_snum[0] == '*') {
		/* Print the Palm's serial number. */
		/* Special serial numbers (currently, only "*Visor*") are
		 * assumed to begin with '*', so anything that begins with
		 * '*' doesn't get a checksum.
		 */
		printf("\tsnum: \"%s\";\n", p_snum);
	} else {
		/* Print the Palm's serial number. */
		printf("\tsnum: \"%s-%c\";\n",
		       p_snum,
		       snum_checksum(p_snum, p_snum_len));
	}

	/* "directory:" line in PDA block */
	if ((pda != NULL) && (pda->directory != NULL) &&
	    (pda->directory[0] != '\0'))
		printf("\tdirectory: \"%s\";\n", pda->directory);

	/* "username:" line in PDA block */
	if ((p_username == NULL) || (p_username[0] == '\0'))
		printf("\tusername: \"%s\";\n", userinfo.fullname);
	else
		printf("\tusername: \"%s\";\n", p_username);

	/* "userid:" line in PDA block */
	if (p_userid == 0)
		printf("\tuserid: %ld;\n",
		       (long) userinfo.uid);
	else
		printf("\tuserid: %ld;\n", p_userid);

	/* PDA block closing brace. */
	printf("}\n");
}

/* find_pda_block
 * Helper function. Finds the best-matching PDA block for the given Palm.
 * Returns a pointer to it if one was defined in the config file(s), or
 * NULL otherwise.
 *
 * If 'check_user' is false, find_pda_block() only checks the serial
 * number. If 'check_user' is true, it also checks the user name and ID. If
 * the user name or ID aren't set in a PDA block, they act as wildcards, as
 * far as find_pda_block() is concerned.
 */
pda_block *
find_pda_block(struct Palm *palm, const Bool check_user)
{
	pda_block *cur;		/* The pda-block we're currently looking at */
	pda_block *default_pda;	/* pda_block for the default PDA, if no
				 * better match is found.
				 */
	const char *p_snum;	/* Serial number of Palm */

	MISC_TRACE(4)
	{
		fprintf(stderr, "Looking for a PDA block.\n");
		if (check_user)
			fprintf(stderr, "  Also checking user info\n");
		else
			fprintf(stderr, "  But not checking user info\n");
	}

	/* Get serial number */
	p_snum = palm_serial(palm);
	if ((p_snum == NULL) && !palm_ok(palm))
		return NULL;

	default_pda = NULL;
	for (cur = sync_config->pda; cur != NULL; cur = cur->next)
	{
		MISC_TRACE(5)
			fprintf(stderr, "Checking PDA \"%s\"\n",
				(cur->name == NULL ? "" : cur->name));

		/* See if this pda_block has a serial number and if so,
		 * whether it matches the one we read off of the Palm.
		 * Palms with pre-3.0 ROMs don't have serial numbers. They
		 * can be represented in the .coldsyncrc file with
		 *	snum "";
		 * This does mean that you shouldn't have more than one
		 * pda_block with an empty string.
		 */
		/* XXX - p_snum has been known to be NULL. Why? Does it
		 * happen when the Palm has a password set and we haven't
		 * sent the appropriate magic to the Palm?
		 */
		if ((cur->snum != NULL) && (p_snum != NULL) &&
		    (strncasecmp(cur->snum, p_snum, SNUM_MAX)
		     != 0))
		{
			/* The serial number doesn't match */
			MISC_TRACE(5)
				fprintf(stderr,
					"\tSerial number doesn't match\n");

			continue;
		}

		/* XXX - Ought to see if the serial number is a real one,
		 * or if it's binary bogosity (on the Visor).
		 */

		/* Check the username and userid, if asked */
		if (check_user)
		{
			const char *p_username;	/* Username on Palm */
			udword p_userid;	/* User ID on Palm */

			/* Check the user ID */
			p_userid = palm_userid(palm);
			if ((p_userid == 0) && !palm_ok(palm))
			{
				/* Something went wrong */
				return NULL;
			}

			if (cur->userid_given &&
			    (cur->userid != p_userid))
			{
				MISC_TRACE(5)
					fprintf(stderr,
						"\tUserid doesn't match\n");
				continue;
			}

			/* Check the user name */
			p_username = palm_username(palm);
			if ((p_username == NULL) && !palm_ok(palm))
			{
				/* Something went wrong */
				return NULL;
			}

			if ((cur->username != NULL) &&
			    strncmp(cur->username, p_username,
				    DLPCMD_USERNAME_LEN) != 0)
			{
				MISC_TRACE(5)
					fprintf(stderr,
						"\tUsername doesn't match\n");
				continue;
			}
		}

		MISC_TRACE(3)
		{
			fprintf(stderr, "Found a match for this PDA:\n");
			fprintf(stderr, "\tS/N: [%s]\n", cur->snum);
			fprintf(stderr, "\tDirectory: [%s]\n",
				cur->directory);
		}

		if ((cur->flags & PDAFL_DEFAULT) != 0)
		{
			MISC_TRACE(3)
				fprintf(stderr,
					"This is a default PDA\n");

			/* Mark this as the default pda_block */
			default_pda = cur;
			continue;
		}

		/* If we get this far, then the serial number matches and
		 * this is not a default pda_block. So this is the one we
		 * want to use.
		 */
		return cur;
	}

	/* If we get this far, then there's no non-default matching PDA. */
	MISC_TRACE(3)
		fprintf(stderr, "No exact match found for "
			"this PDA. Using default\n");

	return default_pda;
			/* 'default_pda' may or may not be NULL. In either
			 * case, this does the Right Thing: if it's
			 * non-NULL, then this function returns the default
			 * PDA block. If 'default_pda' is NULL, then the
			 * config file contains neither a good matching
			 * PDA, nor a default, so it should return NULL
			 * anyway.
			 */
}

/* new_pda_block
 * Allocate and initialize a new pda_block.
 * Returns a pointer to the new pda_block, or NULL in case of error.
 */
pda_block *
new_pda_block()
{
	pda_block *retval;

	/* Allocate the new pda_block */
	if ((retval = (pda_block *) malloc(sizeof(pda_block))) == NULL)
		return NULL;

	/* Initialize the new pda_block */
	retval->next		= NULL;
	retval->flags		= 0;
	retval->name		= NULL;
	retval->snum		= NULL;
	retval->directory	= NULL;
	retval->username	= NULL;
	retval->userid_given	= False;
	retval->userid		= 0L;
	retval->forward		= False;
	retval->forward_host	= NULL;
	retval->forward_name	= NULL;

	return retval;
}

/* free_pda_block
 * Free a pda block. Note that this function does not pay attention to any
 * other pda_blocks on the list. If you have a list of pda_blocks, you can
 * use this function to free each individual element in the list, but not
 * the whole list.
 */
void
free_pda_block(pda_block *p)
{
	if (p->name != NULL)
		free(p->name);
	if (p->snum != NULL)
		free(p->snum);
	if (p->directory != NULL)
		free(p->directory);
	if (p->username != NULL)
		free(p->username);
	if (p->forward_host != NULL)
		free(p->forward_host);
	if (p->forward_name != NULL)
		free(p->forward_name);
	free(p);
}

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
