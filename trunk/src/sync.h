/* sync.h
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	Copyright (C) 2002, Alessandro Zummo.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id: sync.h,v 2.1 2002-07-18 16:43:16 azummo Exp $
 */

extern int do_sync(pda_block *pda, struct Palm *palm);
extern int UpdateUserInfo2(struct Palm *palm, struct dlp_setuserinfo *newinfo);

