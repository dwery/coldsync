#ifndef __pconn_h__
#define __pconn_h__

#define CARD0	0		/* Memory card #0. The only real purpose of
				 * this is to make it marginally easier to
				 * find all of the places where card #0 has
				 * been hardcoded, once support for
				 * multiple memory cards is added, if it
				 * ever is.
				 */

#include <pconn/dlp_cmd.h>

/* XXX - Instead of including a bunch of files here, should just put
 * all of the libpconn prototypes here.
 */
#include <pconn/palm_errno.h>
#include <pconn/cmp.h>
#include <pconn/dlp_cmd.h>
#include <pconn/util.h>

#endif	/* __pconn_h__ */
