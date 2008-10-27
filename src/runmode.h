/* runmode.h
 *
 *	Copyright (C) 1999-2001, Andrew Arensburger.
 *	Copyright (C) 2002, Alessandro Zummo.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id$
 */

extern int run_mode_Standalone(int argc, char *argv[]);
extern int run_mode_Backup(int argc, char *argv[]);
extern int run_mode_Restore(int argc, char *argv[]);
extern int run_mode_Init(int argc, char *argv[]);   
extern int run_mode_Daemon(int argc, char *argv[]);
extern int run_mode_List(int argc, char *argv[]);
extern int run_mode_Info(int argc, char *argv[]);
