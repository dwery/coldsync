/* cs_error.c
 *
 * pconn errors/status -> cs_errno mapping.
 *
 *	Copyright (C) 2002, Alessandro Zummo.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: cs_error.c,v 2.8 2002-11-23 17:19:47 azummo Exp $
 */
#include "config.h"
#include "coldsync.h"
#include "cs_error.h"

/* Include I18N-related stuff, if necessary */
#if HAVE_LIBINTL_H
#  include <locale.h>		/* For setlocale() and friends */
#  include <libintl.h>
#endif  /* HAVE_LIBINTL_H */

void
update_cs_errno_pconn(PConnection *pconn, palmerrno_t palm_errno)
{
	MISC_TRACE(10)
		fprintf(stderr, "update_cs_errno_pconn: %d\n", palm_errno);

	/* Avoid overwriting the current error */
	/* XXX - Is this correct? */
	if (cs_errno == CSE_NOERR)	
	{
		switch (palm_errno)
		{
			case PALMERR_NOERR:
				break;

			case PALMERR_TIMEOUT:
			case PALMERR_EOF:
				cs_errno = CSE_NOCONN;
				break;
			default:
				/* Warn("Setting CSE_PALMERR due to palm_errno = %d.", palm_errno); */
				cs_errno = CSE_PALMERR;
				break;
		}
	}
}

void
update_cs_errno_dlp(PConnection *pconn)
{
	MISC_TRACE(10)
		fprintf(stderr, "update_cs_errno_dlp: %d\n", pconn->dlp.resp.error);

	/* Updates cs_errno only if had no errors 'till now or if
	 * the latest one was CSE_DLPERR
	 */
	 
	if (cs_errno == CSE_NOERR || cs_errno == CSE_DLPERR)	
	{
		switch (pconn->dlp.resp.error)
		{
			case DLPSTAT_NOERR:
				cs_errno = CSE_NOERR;
				break;
				
			case DLPSTAT_CANCEL:
				cs_errno = CSE_CANCEL;
				break;
			default:
				/* Warn("Setting CSE_DLPERR due to dlp error = %d.", pconn->dlp.resp.error); */
				cs_errno = CSE_DLPERR;
				break;
		}
	}

	/* XXX - Btw a lower level error (CSE_PALMERR) would probably
	 * have an higher precedence than CSE_DLPERR, so maybe the only
	 * DLP error we are interested in is DLPSTAT_CANCEL... ?
	 */
}

void
print_cs_errno(CSErrno cs_errno)
{
	switch (cs_errno)
	{
		case CSE_NOERR:
			break;

		case CSE_OTHER:
			Error(_("Other error."));
			break;
	
		case CSE_PALMERR:
			Error(_("Communication error."));
			break;
	
		case CSE_DLPERR:
			Error(_("DLP error."));
			break;
	
		case CSE_NOCONN:
			Error(_("Lost connection to Palm."));
			break;

		case CSE_CANCEL:
			Warn(_("Sync cancelled by Palm."));
			break;

		default:
			Error(_("Unknown error (?!?)."));
			break;
	}
}

/* XXX - This function doesn't really belong to cs_error.c */
void
print_latest_dlp_error(PConnection *pconn)
{
	dlp_stat_t err = dlp_latest_error(pconn);
 	
	if (err != DLPSTAT_NOERR)
		Error(_("DLP error %d: %s."),
			err,
			dlp_strerror(err));
}
                    
/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
