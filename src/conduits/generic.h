/* generic.h
 *
 * Generic conduit, header file.
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: generic.h,v 1.1 2002-12-10 16:55:31 azummo Exp $
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>			/* For malloc(), free() */

#if HAVE_LIBINTL_H
#  include <libintl.h>			/* For i18n */
#endif	/* HAVE_LIBINTL_H */

#include <pconn/pconn.h>
#include "conduit.h"

extern int run_GenericConduit(PConnection *pconn,
		     const struct dlp_dbinfo *dbinfo,
		     const conduit_block *block,
		     const pda_block *pda);

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
