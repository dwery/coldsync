/* spc_client.h
 *
 * Allows a conduit to connect to the Palm device using SPC.
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * This file was modified by Fred Gylys-Colwell
 *
 * $Id: spc_client.h,v 1.1.2.1 2001-10-09 01:41:24 arensb Exp $
 */
#ifndef _spc_client_h_
#define _spc_client_h_

#include "pconn/PConnection.h"
#include "pconn/dlp_cmd.h"
#include "pconn/netsync.h"

/* Create a PConnection for a conduit to using SLP. */
extern PConnection *new_spc_client(int fd);
/* Get the database information  using SPC.*/
extern int spc_get_dbinfo(PConnection *pconn, struct dlp_dbinfo *info);

#endif	/* _spc_client_h_ */
