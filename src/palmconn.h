/* palmconn.h
 *
 *      Copyright (C) 1999-2000, Andrew Arensburger.
 *	Copyright (C) 2002, Alessandro Zummo.
 *      You may distribute this file under the terms of the Artistic
 *      License, as specified in the README file.
 *
 * $Id: palmconn.h,v 2.3 2002-09-04 14:19:09 azummo Exp $
 */

#ifndef _palmconn_h_
#define _palmconn_h_  

extern struct Palm * palm_Connect(void);
extern void palm_Disconnect(struct Palm *palm, ubyte status);
extern void palm_Release(struct Palm *palm, ubyte status);
extern void palm_CSDisconnect(struct Palm *palm);

#endif
