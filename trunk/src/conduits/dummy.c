/* dummy.c
 *
 * Dummy conduit.
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: dummy.c,v 1.1 2002-12-10 11:54:51 azummo Exp $
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>			/* For malloc(), free() */

#if HAVE_LIBINTL_H
#  include <libintl.h>			/* For i18n */
#endif	/* HAVE_LIBINTL_H */

#include "dummy.h"

int run_DummyConduit(PConnection *pconn,
		     const struct dlp_dbinfo *dbinfo,
		     const conduit_block *block,
		     const pda_block *pda);


/* XXX - Experimental */
int run_DummyConduit(PConnection *pconn,
		     const struct dlp_dbinfo *dbinfo,
		     const conduit_block *block,
		     const pda_block *pda)
{
	fprintf(stderr, _("Inside run_DummyConduit.\n"));
	return 0;
}


/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
