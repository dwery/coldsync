/* cs_error.c
 *
 * pconn->palm_errno -> cs_errno mapping.
 * This is a temporary hac to avoid breaking things.
 *
 *	Copyright (C) 2002, Alessandro Zummo.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: cs_error.c,v 2.1 2002-04-27 18:00:07 azummo Exp $
 */
#include "config.h"
#include "coldsync.h"
#include "cs_error.h"


void
update_cs_errno_p(PConnection *pconn)
{
                switch (PConn_get_palmerrno(pconn))
                {
                    case PALMERR_TIMEOUT:
                  	cs_errno = CSE_NOCONN;
                        break;
                    default:
                        break;
                }
}


/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
