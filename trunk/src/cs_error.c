/* cs_error.c
 *
 * pconn errors/status -> cs_errno mapping.
 *
 *	Copyright (C) 2002, Alessandro Zummo.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: cs_error.c,v 2.2 2002-08-31 19:26:03 azummo Exp $
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
				cs_errno = CSE_OTHER;
				break;
		}
	}
}

void
update_cs_errno_dlp(PConnection *pconn)
{
	MISC_TRACE(10)
		fprintf(stderr, "update_cs_errno_dlp: %d\n", pconn->dlp.resp.error);

	/* Avoid overwriting the current error */
	if (cs_errno == CSE_NOERR)	
	{
		switch (pconn->dlp.resp.error)
		{
			case DLPSTAT_NOERR:
				break;
				
			case DLPSTAT_CANCEL:
				cs_errno = CSE_CANCEL;
				break;
			default:
				cs_errno = CSE_OTHER;
				break;
		}
	}
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
		Error(_("DLP error: [%d] %s\n"),
			err,
			dlp_strerror(err));
}
                    
/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
