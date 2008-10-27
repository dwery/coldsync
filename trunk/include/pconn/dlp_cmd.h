/* dlp_cmd.h
 *
 * Definitions and types for the DLP convenience functions.
 *
 *	Copyright (C) 1999-2000, Andrew Arensburger.
 *	You may distribute this file under the terms of the Artistic
 *	License, as specified in the README file.
 *
 * $Id$
 */
#ifndef _dlp_cmd_h_
#define _dlp_cmd_h_

#include "palm.h"
#include "PConnection.h"

/* XXX - The DLPRETLEN_* macros are never used. Should they be? Or should
 * they just be nuked?
 */

/* DLP command codes
 * These enum elements are all given explicit values because they're
 * externally defined by Palm, and the values have to match up.
 */
typedef enum {
	/* 1.0 functions */
	DLPCMD_ReadUserInfo		= 0x10,	/* Get user info */
	DLPCMD_WriteUserInfo		= 0x11,	/* Set user info */
	DLPCMD_ReadSysInfo		= 0x12,	/* Get system info */
	DLPCMD_GetSysDateTime		= 0x13,	/* Get the time on the Palm */
	DLPCMD_SetSysDateTime		= 0x14,	/* Set time on the Palm */
	DLPCMD_ReadStorageInfo		= 0x15,	/* Get memory info */
	DLPCMD_ReadDBList		= 0x16,	/* Read database list */
	DLPCMD_OpenDB			= 0x17,	/* Open a database */
	DLPCMD_CreateDB			= 0x18,	/* Create a new database */
	DLPCMD_CloseDB			= 0x19,	/* Close database(s) */
	DLPCMD_DeleteDB			= 0x1a,	/* Delete a database */
	DLPCMD_ReadAppBlock		= 0x1b,	/* Read AppInfo block */
	DLPCMD_WriteAppBlock		= 0x1c,	/* Write AppInfo block */
	DLPCMD_ReadSortBlock		= 0x1d,	/* Read app sort block */
	DLPCMD_WriteSortBlock		= 0x1e,	/* Write app sort block */
	DLPCMD_ReadNextModifiedRec	= 0x1f,	/* Read next modified record */
	DLPCMD_ReadRecord		= 0x20,	/* Read a record */
	DLPCMD_WriteRecord		= 0x21,	/* Write a record */
	DLPCMD_DeleteRecord		= 0x22,	/* Delete records */
	DLPCMD_ReadResource		= 0x23,	/* Read a resource */
	DLPCMD_WriteResource		= 0x24,	/* Write a resource */
	DLPCMD_DeleteResource		= 0x25,	/* Delete a resource */
	DLPCMD_CleanUpDatabase		= 0x26,	/* Purge deleted records */
	DLPCMD_ResetSyncFlags		= 0x27,	/* Reset dirty flags */
	DLPCMD_CallApplication		= 0x28,	/* Call an application */
	DLPCMD_ResetSystem		= 0x29,	/* Reset at end of sync */
	DLPCMD_AddSyncLogEntry		= 0x2a,	/* Write the sync log */
	DLPCMD_ReadOpenDBInfo		= 0x2b,	/* Get info about an open DB */
	DLPCMD_MoveCategory		= 0x2c,	/* Move records in a
						 * category */
	DLPCMD_ProcessRPC		= 0x2d,	/* Remote Procedure Call */
	DLPCMD_OpenConduit		= 0x2e,	/* Say a conduit is open */
	DLPCMD_EndOfSync		= 0x2f,	/* Terminate the sync */
	DLPCMD_ResetRecordIndex		= 0x30,	/* Reset "modified" index */
	DLPCMD_ReadRecordIDList		= 0x31,	/* Get list of record IDs */
	/* DLP 1.1 functions */
	DLPCMD_ReadNextRecInCategory	= 0x32,	/* Next record in category */
	DLPCMD_ReadNextModifiedRecInCategory	= 0x33,
						/* Next modified record in
						 * category */
	DLPCMD_ReadAppPreference	= 0x34,	/* Read app preference */
	DLPCMD_WriteAppPreference	= 0x35,	/* Write app preference */
	DLPCMD_ReadNetSyncInfo		= 0x36,	/* Read NetSync info */
	DLPCMD_WriteNetSyncInfo		= 0x37,	/* Write NetSync info */
	DLPCMD_ReadFeature		= 0x38,	/* Read a feature */

	/* DLP 1.2 functions (PalmOS v3.0) */
	DLPCMD_FindDB			= 0x39,	/* Find a database given
						 * creator/type or name */
	DLPCMD_SetDBInfo		= 0x3a,	/* Change database info */
	DLPCMD_LoopBackTest		= 0x3b, /* Perform a loopback test */
	DLPCMD_ExpSlotEnumerate		= 0x3c, /* Get the number of slots on the device */
	DLPCMD_ExpCardPresent		= 0x3d, /* Check if the card is present*/
	DLPCMD_ExpCardInfo		= 0x3e  /* Get infos on the installed exp card*/
} dlpc_op_t;

/* dlp_time
 * Structure for giving date and time. If the year is zero, then this is a
 * nonexistent date, regardless of what values the other fields may have.
 */
struct dlp_time
{
	uword year;		/* Year (4 digits) */
	ubyte month;		/* Month, 1-12 */
	ubyte day;		/* Day of month, 1-31 */
	ubyte hour;		/* Hour, 0-23 */
	ubyte minute;		/* Minute, 0-59 */
	ubyte second;		/* Second, 0-59 */
	ubyte unused;		/* Unused. Set to 0 */
};

/* Argument IDs, by command.
 * In each section:
 * - DLPARG_<func>_<type> is the request ID for function <func>. Some
 *	functions can pass several types of arguments; <type> serves
 *	to distinguish between them. Functions with only one argument
 *	use DLPARG_BASE as the request ID. Functions with multiple
 *	arguments typically use DLPARG_BASE, DLPARG_BASE+1, ... as
 *	their request IDs.
 * - DLPARGLEN_<func>_<type> is the length, in bytes, of request ID
 *	DLPARG_<func>_<type>. Some arguments have a variable length;
 *	of those, some consist of a fixed-size header followed by
 *	variable-length data. In these cases, DLPARGLEN_<func>_<type>
 *	is the length of the fixed-sized header. (In other words,
 *	check the spec: don't assume that DLPARGLEN_<func>_<type>
 *	gives the full length of the argument.)
 * - DLPRET_<func>_<type> is the response ID for function <func>. Some
 *	functions can return several types of arguments (or multiple
 *	ones); <type> serves to distinguish between them. Response IDs
 *	begin at DLPRET_BASE, and typically go up by one for each
 *	additional type of response.
 * -DLPRETLEN_<func>_<type> is the length of response ID
 *	DLPRET_<func>_<type>. Some responses have a variable length;
 *	of those, most consist of a fixed-size header followed by
 *	variable-length data. In these cases, DLPRETLEN_<func>_<type>
 *	is the length of the fixed-sized header. (In other words,
 *	check the spec: don't assume that DLPRETLEN_<func>_<type>
 *	gives the full length of the response.)
 */
#define DLPARG_BASE			0x20
#define DLPRET_BASE			DLPARG_BASE

/** ReadUserInfo **/
#define DLPRET_ReadUserInfo_Info	DLPRET_BASE
#define DLPRETLEN_ReadUserInfo_Info	30

#define DLPCMD_USERNAME_LEN		41	/* Max. length of user
						 * name, including
						 * terminating NUL. (from
						 * dlpMaxUserNameSize)
						 * */

/* dlp_userinfo
 * The data returned from a DlpReadUserInfo command.
 */
struct dlp_userinfo
{
	udword userid;		/* User ID number (0 if none) */
	udword viewerid;	/* ID assigned to viewer by desktop. Not
				 * currently used, according to Palm:
				 * http://oasis.palm.com/dev/kb/manuals/1706.cfm
				 */
	udword lastsyncPC;	/* Last sync PC ID (0 if none) */
	struct dlp_time lastgoodsync;
				/* Time of last successful sync */
	struct dlp_time lastsync;
				/* Time of last sync attempt */
	ubyte usernamelen;	/* Length of username, including NUL
				 * (0 if no username) */
	ubyte passwdlen;	/* Length of encrypted password (0 if none) */
	char username[DLPCMD_USERNAME_LEN];	/* Username */
	ubyte passwd[256];	/* XXX - This is 32 bytes long on a
				 * PalmPilot, but I don't know how it can
				 * vary, nor how the password is encrypted.
				 * There are indications that the Palm
				 * understands DES, MD4 and MD5.
				 */
};

/** WriteUserInfo **/
#define DLPARG_WriteUserInfo_UserInfo	DLPARG_BASE
#define DLPARGLEN_WriteUserInfo_UserInfo	22
						/* Doesn't include user
						 * name */

/* dlp_setuserinfo
 * This data structure is passed to DlpWriteUserInfo, to change (parts
 * of) the user info.
 */
struct dlp_setuserinfo
{
	udword userid;		/* User ID number */
	udword viewerid;	/* ID assigned to viewer by desktop (?) */
	udword lastsyncPC;	/* Last sync PC ID */
	struct dlp_time lastsync;
				/* Time of last successful sync */
	ubyte modflags;		/* Flags indicating which values have
				 * changed (DLPCMD_MODUIFLAG_*, below) */
	ubyte usernamelen;	/* Length of username, including NUL */
				/* XXX - Probably better to determine from
				 * string (but what if the string isn't
				 * terminated, for whatever reason?)
				 */
	const char *username;	/* User name */
				/* XXX - This should probably be an array,
				 * of length DLPCMD_USERNAME_LEN
				 */
};

/* Possible values for the 'flags' field in a 'struct dlp_setuserinfo'.
 * These flags should be or-ed together when one is changing several values.
 */
#define DLPCMD_MODUIFLAG_USERID		0x80	/* User ID */
#define DLPCMD_MODUIFLAG_SYNCPC		0x40	/* Last sync PC */
#define DLPCMD_MODUIFLAG_SYNCDATE	0x20	/* Sync date */
#define DLPCMD_MODUIFLAG_USERNAME	0x10	/* User name */
#define DLPCMD_MODUIFLAG_VIEWERID	0x08	/* Viewer ID */

/** ReadSysInfo **/
#define DLPARG_ReadSysInfo_Ver		DLPARG_BASE
#define DLPARGLEN_ReadSysInfo_Ver	4

#define DLPRET_ReadSysInfo_Info		DLPRET_BASE
#define DLPRETLEN_ReadSysInfo_Info	14

#define DLPRET_ReadSysInfo_Ver		DLPRET_BASE+1	/* v1.2 extension */
#define DLPRETLEN_ReadSysInfo_Ver	12

/* dlp_sysinfo
 * The data returned from a DlpReadSysInfo command.
 */
struct dlp_sysinfo
{
	udword rom_version;	/* ROM system software version */
	udword localization;	/* Localization ID (?) */
	ubyte unused;		/* Set this to 0 */
	ubyte prodIDsize;	/* Size of product/ID field */
	udword prodID;		/* Product ID */
				/* The headers say that the product ID used
				 * to be a variable-sized field. If
				 * someone's collecting ancient Palms, this
				 * could be a source of bugs.
				 */

	/* DLP 1.2 fields. Not all Palms will return these, in which case
	 * they will be set to 0.
	 */
	uword dlp_ver_maj;	/* Palm's DLP major version */
	uword dlp_ver_min;	/* Palm's DLP minor version */
	uword comp_ver_maj;	/* Palm's product compatibility major
				 * version */
	uword comp_ver_min;	/* Palm's product compatibility minor
				 * version */
	udword max_rec_size;	/* Max. size of record that can be
				 * allocated on Palm, assuming sufficient
				 * memory. 0xffffffff == all available
				 * memory.
				 */
};

/** GetSysDateTime **/
#define DLPRET_GetSysDateTime_Time	DLPRET_BASE
#define DLPRETLEN_GetSysDateTime_Time	8

/** SetSysDateTime **/
#define DLPARG_SetSysDateTime_Time	DLPARG_BASE
#define DLPARGLEN_SetSysDateTime_Time	8

/** ReadStorageInfo **/
#define DLPARG_ReadStorageInfo_Req	DLPARG_BASE
#define DLPARGLEN_ReadStorageInfo_Req	2

#define DLPRET_ReadStorageInfo_Info	DLPRET_BASE
#define DLPRETLEN_ReadStorageInfo_Info	64

#define DLPRET_ReadStorageInfo_Ext	DLPRET_BASE+1	/* v1.1 */
#define DLPRETLEN_ReadStorageInfo_Ext	32

#define DLPCMD_MEMCARD_LEN	32	/* Max. length of card and
					 * manufacturer name, including
					 * NUL.
					 */

struct dlp_cardinfo
{
	ubyte totalsize;	/* Total size of this card info */
	ubyte cardno;		/* Card number */
	uword cardversion;	/* Card version */
	struct dlp_time ctime;	/* Creation date and time */
	udword rom_size;	/* ROM size */
	udword ram_size;	/* RAM size */
	udword free_ram;	/* Free RAM size */
	ubyte cardname_size;	/* Size of card name string (not counting
				 * NUL) */
	ubyte manufname_size;	/* Size of manufacturer name string (not
				 * counting NUL) */
	char cardname[DLPCMD_MEMCARD_LEN];
				/* Card name */
	char manufname[DLPCMD_MEMCARD_LEN];
				/* Manufacturer name */

	/* DLP 1.1 extensions */
	uword rom_dbs;		/* ROM database count */
	uword ram_dbs;		/* RAM database count */
	udword reserved1;	/* Set to 0 */
	udword reserved2;	/* Set to 0 */
	udword reserved3;	/* Set to 0 */
	udword reserved4;	/* Set to 0 */
};

/** ReadDBList **/
#define DLPARG_ReadDBList_Req		DLPARG_BASE
#define DLPARGLEN_ReadDBList_Req	4

#define DLPRET_ReadDBList_Info		DLPRET_BASE
#define DLPRETLEN_ReadDBList_Info	4

/* Flags for ReadDBList */
#define DLPCMD_READDBLFLAG_RAM		0x80		/* Search RAM DBs */
#define DLPCMD_READDBLFLAG_ROM		0x40		/* Search ROM DBs */
#define DLPCMD_READDBLFLAG_MULT		0x20		/* OK to return
							 * multiple entries
							 * (DLP v1.2) */

#define DLPRET_READDBLFLAG_MORE		0x80	/* There are more database
						 * descriptions coming.
						 */

#define DLPCMD_DBNAME_LEN	32	/* 31 chars + NUL (from
					 * dmDBNameLength)
					 */

struct dlp_dbinfo
{
	ubyte size;		/* Total size of the DB info */
	ubyte misc_flags;	/* v1.1 flags (DLPCMD_DBINFOFL_*, below) */
	uword db_flags;		/* Database flags (DLPCMD_DBFLAG_*, below) */
	udword type;		/* Database type */
	udword creator;		/* Database creator */
	uword version;		/* Database version */
	udword modnum;		/* Modification number (?) */
	struct dlp_time ctime;	/* Creation time */
	struct dlp_time mtime;	/* Last modification time */
	struct dlp_time baktime;	/* Last backup time */
	uword index;		/* Database index */

	char name[DLPCMD_DBNAME_LEN];
				/* Database name, NUL-terminated */
};
#define DLPCMD_DBINFO_LEN	44	/* Size of 'struct dlp_dbinfo',
					 * not counting 'name'
					 */

/* XXX - These are the same as PDB_ATTR_* in "pdb.h", and shouldn't be
 * duplicated.
 */
#define DLPCMD_DBFLAG_RESDB	0x0001	/* This is a resource database
					 * (record database otherwise)
					 */
#define DLPCMD_DBFLAG_RO	0x0002	/* Read-only database */
#define DLPCMD_DBFLAG_APPDIRTY	0x0004	/* App. info block is dirty */
#define DLPCMD_DBFLAG_BACKUP	0x0008	/* Database should be backed up if
					 * no app-specific conduit has been
					 * supplied.
					 */
#define DLPCMD_DBFLAG_OKNEWER	0x0010	/* Tells backup conduit that it's
					 * okay to install a newer version
					 * of this database with a different
					 * name if the current database is
					 * open (e.g., Graffiti Shortcuts).
					 */
#define DLPCMD_DBFLAG_RESET	0x0020	/* Reset device after installing */
#define DLPCMD_DBFLAG_OPEN	0x8000	/* Database is open */

#define DLPCMD_DBINFOFL_EXCLUDE	0x80	/* Exclude from sync (DLP v1.1) */
#define DLPCMD_DBINFOFL_RAM	0x40	/* RAM-based(?) (DLP v1.2) */

#define DBINFO_ISRSRC(dbinfo)	((dbinfo)->db_flags & DLPCMD_DBFLAG_RESDB)
#define DBINFO_ISROM(dbinfo)	((dbinfo)->db_flags & DLPCMD_DBFLAG_RO)

/** OpenDB **/
#define DLPARG_OpenDB_DB		DLPARG_BASE
#define DLPARGLEN_OpenDB_DB		2

#define DLPRET_OpenDB_DB		DLPRET_BASE
#define DLPRETLEN_OpenDB_DB		1

/* Modes for opening databases */
#define DLPCMD_MODE_SECRET	0x10	/* Show secret (?) */
#define DLPCMD_MODE_EXCLUSIVE	0x20	/* ??? */
#define DLPCMD_MODE_WRITE	0x40	/* Writing */
#define DLPCMD_MODE_READ	0x80	/* Reading */

/** CreateDB **/
#define DLPARG_CreateDB_DB		DLPARG_BASE
#define DLPARGLEN_CreateDB_DB		14

#define DLPRET_CreateDB_DB		DLPRET_BASE
#define DLPRETLEN_CreateDB_DB		1

/* Request to create a new database */
struct dlp_createdbreq
{
	udword creator;		/* Database creator */
	udword type;		/* Database type */
	ubyte card;		/* Memory card number */
	ubyte unused;		/* Set to 0 */
	uword flags;		/* Flags. See DLPCMD_DBFLAG_* */
	uword version;		/* Database version */
	char name[DLPCMD_DBNAME_LEN];		/* Database name */
				/* I'm not 100% sure about this length, but
				 * it certainly seems plausible.
				 */
};

/** CloseDB **/
#define DLPARG_CloseDB_One		DLPARG_BASE
#define DLPARGLEN_CloseDB_One		1
#define DLPARG_CloseDB_All		DLPARG_BASE+1
#define DLPARGLEN_CloseDB_All		0
#define DLPARG_CloseDB_Update		DLPARG_BASE+2	/* Close a specific
							 * DB and update
							 * mod times
							 * (PalmOS 3.0)
							 */
#define DLPARGLEN_CloseDB_Update	2

#define DLPCMD_CLOSEALLDBS	0xff	/* Close all databases */

/* CloseDB flags for PalmOS v3.0 (not supported yet) */
#define DLPCMD_CLOSEFL_UPBACKUP	0x80	/* Update backup time */
#define DLPCMD_CLOSEFL_UPMOD	0x40	/* Update modification time */
#define DLPCMD_CLOSEFL_ALL	(DLPCMD_CLOSEFL_UPBACKUP | \
				 DLPCMD_CLOSEFL_UPMOD)

/** DeleteDB **/
#define DLPARG_DeleteDB_DB		DLPARG_BASE
#define DLPARGLEN_DeleteDB_DB		2

/** ReadAppBlock **/
#define DLPARG_ReadAppBlock_Req		DLPARG_BASE
#define	DLPARGLEN_ReadAppBlock_Req	6

#define DLPRET_ReadAppBlock_Blk		DLPRET_BASE
#define DLPRETLEN_ReadAppBlock_Blk	2

#define DLPC_APPBLOCK_TOEND		(uword) ~0	/* Read to the end of
							 * the AppInfo block.
							 */

/** WriteAppBlock **/
#define DLPARG_WriteAppBlock_Block	DLPARG_BASE
#define DLPARGLEN_WriteAppBlock_Block	4

/** ReadSortBlock **/
#define DLPARG_ReadSortBlock_Req	DLPARG_BASE
#define	DLPARGLEN_ReadSortBlock_Req	6

#define DLPRET_ReadSortBlock_Blk	DLPRET_ReadAppBlock_Blk
#define DLPRETLEN_ReadSortBlock_Blk	DLPRETLEN_ReadAppBlock_Blk

#define DLPC_SORTBLOCK_TOEND		(uword) ~0	/* Read to the end
							 * of the sort
							 * block.
							 */

/** WriteSortBlock **/
#define DLPARG_WriteSortBlock_Block	DLPARG_WriteAppBlock_Block
#define DLPARGLEN_WriteSortBlock_Block	DLPARGLEN_WriteAppBlock_Block

/** ReadNextModifiedRec **/
#define DLPARG_ReadNextModifiedRec_Req	DLPARG_BASE
#define DLPARGLEN_ReadNextModifiedRec_Req	1

#define DLPRET_ReadNextModifiedRec_Rec	DLPARG_BASE
#define DLPRETLEN_ReadNextModifiedRec_Rec	10

/** ReadRecord **/
#define DLPARG_ReadRecord_ByID		DLPARG_BASE
#define DLPARGLEN_ReadRecord_ByID	10
#define DLPARG_ReadRecord_ByIndex	DLPARG_BASE+1
#define DLPARGLEN_ReadRecord_ByIndex	8

#define DLPRET_ReadRecord_Rec		DLPRET_BASE
#define DLPRETLEN_ReadRecord_Rec	10

#define DLPC_RECORD_TOEND		(uword) ~0	/* Read the entire
							 * record.
							 */
/* dlp_recinfo is used by both DlpReadRecordByID() and
 * DlpReadRecordByIndex().
 */
struct dlp_recinfo
{
	udword id;		/* Record ID */
	uword index;		/* Record index */
	uword size;		/* Record size */
	ubyte attributes;	/* Record attributes */
	ubyte category;		/* Record category */
};

/** WriteRecord **/
#define DLPARG_WriteRecord_Rec		DLPARG_BASE
#define DLPARGLEN_WriteRecord_Rec	8

#define DLPRET_WriteRecord_Rec		DLPRET_BASE
#define DLPRETLEN_WriteRecord_Rec	4

/** DeleteRecord **/
#define DLPARG_DeleteRecord_Rec		DLPARG_BASE
#define DLPARGLEN_DeleteRecord_Rec	6

#define DLPCMD_DELRECFLAG_ALL		0x80
					/* Ignore the record ID. Delete all
					 * records */
#define DLPCMD_DELRECFLAG_CATEGORY	0x40
					/* The least significant byte of
					 * the record ID contains the
					 * category of records to be
					 * deleted.
					 */
/** ReadResource **/
#define DLPARG_ReadResource_ByIndex	DLPARG_BASE
#define DLPARGLEN_ReadResource_ByIndex	8
#define DLPARG_ReadResource_ByType	DLPARG_BASE+1
#define DLPARGLEN_ReadResource_ByType	12

#define DLPRET_ReadResource_Rsrc	DLPRET_BASE
#define DLPRETLEN_ReadResource_Rsrc	10

#define DLPC_RESOURCE_TOEND		(uword) ~0	/* Read the entire
							 * resource.
							 */

struct dlp_resource
{
	udword type;		/* Resource type */
	uword id;		/* Resource ID */
	uword index;		/* Resource index */
	uword size;		/* Resource size */
};

/** WriteResource **/
#define DLPARG_WriteResource_Rsrc	DLPARG_BASE
#define DLPARGLEN_WriteResource_Rsrc	10

/** DeleteResource **/
#define DLPARG_DeleteResource_Res	DLPARG_BASE
#define DLPARGLEN_DeleteResource_Res	8

#define DLPCMD_DELRSRCFLAG_ALL		0x80
					/* Delete all resources in the DB */

/** CleanUpDatabase **/
#define DLPARG_CleanUpDatabase_DB	DLPARG_BASE
#define DLPARGLEN_CleanUpDatabase_DB	1

/** ResetSyncFlags **/
#define DLPARG_ResetSyncFlags_DB	DLPARG_BASE
#define DLPARGLEN_ResetSyncFlags_DB	1

/** CallApplication **/
#define DLPARG_CallApplication_V1	DLPARG_BASE
#define DLPARGLEN_CallApplication_V1	6
#define DLPARG_CallApplication_V2	DLPARG_BASE+1
#define DLPARGLEN_CallApplication_V2	22

#define DLPRET_CallApplication_V1	DLPRET_BASE
#define DLPRETLEN_CallApplication_V1	6
#define DLPRET_CallApplication_V2	DLPRET_BASE+1
#define DLPRETLEN_CallApplication_V2	16

struct dlp_appcall
{
	udword creator;		/* Application DB creator */
	udword type;		/* DB type of executable */
				/* (PalmOS >= 2.x) */
	uword action;		/* Action code */

};

struct dlp_appresult
{
	uword action;		/* Action code that was called (v1.0) */
	udword result;		/* Application result error code */
	udword size;		/* # bytes of return data */
	udword reserved1;	/* Set to 0 */
	udword reserved2;	/* Set to 0 */
	ubyte data[2048];	/* Result data */
				/* XXX - Fixed size: bad! Could this be
				 * turned into a 'const ubyte *', with a
				 * pointer into the DLP layer's buffer,
				 * like DlpReadRecordByID()?
				 */
};

/** ResetSystem **/
/* No arguments, nothing returned */

/** AddSyncLogEntry **/
#define DLPARG_AddSyncLogEntry_Msg	DLPARG_BASE

#define DLPC_MAXLOGLEN			2048
			/* The longest log that can be uploaded (including
			 * terminating NUL). */

/** ReadOpenDBInfo **/
#define DLPARG_ReadOpenDBInfo_DB	DLPARG_BASE
#define DLPARGLEN_ReadOpenDBInfo_DB	1

#define DLPRET_ReadOpenDBInfo_Info	DLPRET_BASE
#define DLPRETLEN_ReadOpenDBInfo_Info	2

struct dlp_opendbinfo
{
	/* Yes, it seems bogus to have a struct that contains a single
	 * field, but with a name like ReadOpenDBInfo, this just seems like
	 * the sort of function that might return more information in the
	 * future.
	 */
	uword numrecs;		/* # records in database */
};

/** MoveCategory **/
#define DLPARG_MoveCategory_Cat		DLPARG_BASE
#define DLPARGLEN_MoveCategory_Cat	4

/** ProcessRPC **/
/* ProcessRPC is special. It doesn't use the usual DLP wrappers */

/** OpenConduit **/
/* No arguments, nothing returned */

/** EndOfSync **/
#define DLPARG_EndOfSync_Status		DLPARG_BASE
#define DLPARGLEN_EndOfSync_Status	2

/* Sync termination codes */
#define DLPCMD_SYNCEND_NORMAL	0	/* Normal */
#define DLPCMD_SYNCEND_NOMEM	1	/* Out of memory */
#define DLPCMD_SYNCEND_CANCEL	2	/* User cancelled */
#define DLPCMD_SYNCEND_OTHER	3	/* None of the above */

/** ResetRecordIndex **/
#define DLPARG_ResetRecordIndex_DB	DLPARG_BASE
#define DLPARGLEN_ResetRecordIndex_DB	1

/** ReadRecordIDList **/
#define DLPARG_ReadRecordIDList_Req	DLPARG_BASE
#define DLPARGLEN_ReadRecordIDList_Req	6

#define DLPRET_ReadRecordIDList_List	DLPRET_BASE
#define DLPRETLEN_ReadRecordIDList_List	8

#define DLPCMD_RRIDFLAG_SORT		0x80	/* Sort database before
						 * returning list.
						 */

#define DLPC_RRECIDL_TOEND		(uword) ~0	/* Read all of the
							 * record IDs.
							 */

/* 1.1 functions */
/** ReadNextRecInCategory **/
#define DLPARG_ReadNextRecInCategory_Rec	DLPARG_BASE
#define DLPARGLEN_ReadNextRecInCategory_Rec	2

#define DLPRET_ReadNextRecInCategory_Rec	DLPRET_BASE
#define DLPRETLEN_ReadNextRecInCategory_Rec	10

/** ReadNextModifiedRecInCategory **/
#define DLPARG_ReadNextModifiedRecInCategory_Rec	DLPARG_BASE
#define DLPARGLEN_ReadNextModifiedRecInCategory_Rec	2

#define DLPRET_ReadNextModifiedRecInCategory_Rec	DLPRET_BASE
#define DLPRETLEN_ReadNextModifiedRecInCategory_Rec	10

/** ReadAppPreference **/
#define DLPARG_ReadAppPreference_Pref	DLPARG_BASE
#define DLPARGLEN_ReadAppPreference_Pref	10

#define DLPRET_ReadAppPreference_Pref	DLPRET_BASE
#define DLPRETLEN_ReadAppPreference_Pref	6

/* Both ReadAppPreference() and WriteAppPreference() use this. */
struct dlp_apppref
{
	uword version;		/* Version number */
				/* XXX - App or preference? */
	uword size;		/* Actual size of preference */
	uword len;		/* # bytes returned by
				 * DlpReadAppPreference() */
};

#define DLPC_READAPP_FULL		0xffff	/* Read the entire
						 * preference */
#define DLPC_READAPPFL_BACKEDUP		0x80	/* Read backed up
						 * preference database */

/** WriteAppPreference **/
#define DLPARG_WriteAppPreference_Pref	DLPARG_BASE
#define DLPARGLEN_WriteAppPreference_Pref	12

/** ReadNetSyncInfo **/
#define DLPRET_ReadNetSyncInfo_Info	DLPRET_BASE
#define DLPRETLEN_ReadNetSyncInfo_Info	24

#define DLPCMD_MAXHOSTNAMELEN	256	/* Max. length of a hostname,
					 * including terminating NUL. (from
					 * dlpMaxHostAddrLength in the Palm
					 * header files)
					 */
/* I haven't found a Palm source that gives the official maximum length of
 * the address or netmask strings, but the following values should be large
 * enough to hold an IPv6 address or netmask written in inefficient dotted
 * decimal notation (like IPv4), so they might hold us for a while.
 */
#define DLPCMD_MAXADDRLEN	128	/* Max. length of address string,
					 * including terminating NUL.
					 */
#define DLPCMD_MAXNETMASKLEN	128	/* Max. length of netmask string,
					 * including terminating NUL.
					 */

struct dlp_netsyncinfo
{
	ubyte lansync_on;	/* Non-zero if LAN sync enabled (i.e.,
				 * presumably, if the host described below
				 * is willing to accept NetSyncs.
				 */
	ubyte reserved1;	/* Set to 0 */
	udword reserved1b;	/* Set to 0 */
	udword reserved2;	/* Set to 0 */
	udword reserved3;	/* Set to 0 */
	udword reserved4;	/* Set to 0 */
		/* The hostname, address and netmask are not stored as
		 * binary data (4 bytes for the IP address), but rather as
		 * NUL-terminated strings.
		 * This is arguably good, since it leaves the door open for
		 * IPv6 and other fun stuff.
		 */
	uword hostnamesize;	/* Length of sync host's name, including
				 * terminating NUL. */
	uword hostaddrsize;	/* Length of sync host's address, including
				 * terminating NUL. */
	uword hostnetmasksize;	/* Length of sync host's subnet mask,
				 * including terminating NUL. */

	/* This follows the header */
	char hostname[DLPCMD_MAXHOSTNAMELEN];
				/* Name of sync host */
	char hostaddr[DLPCMD_MAXADDRLEN];
				/* Address of sync host */
	char hostnetmask[DLPCMD_MAXNETMASKLEN];
				/* Netmask of sync host */
};

/** WriteNetSyncInfo **/
#define DLPARG_WriteNetSyncInfo_Info	DLPARG_BASE
#define DLPARGLEN_WriteNetSyncInfo_Info	24

/* Flags to indicate which values are being changed by this
 * WriteNetSyncInfo request.
 */
#define DLPCMD_MODNSFLAG_LANSYNC	0x80	/* LAN sync setting */
#define DLPCMD_MODNSFLAG_HOSTNAME	0x40	/* Sync host name */
#define DLPCMD_MODNSFLAG_HOSTADDR	0x20	/* Sync host address */
#define DLPCMD_MODNSFLAG_NETMASK	0x10	/* Sync host netmask */

/** ReadFeature **/
#define DLPARG_ReadFeature_Req		DLPARG_BASE
#define DLPARGLEN_ReadFeature_Req	6

#define DLPRET_ReadFeature_Feature	DLPRET_BASE
#define DLPRETLEN_ReadFeature_Feature	4

/* 1.2 functions */
/** FindDB **/
#define DLPARG_FindDB_ByName		DLPARG_BASE
#define DLPARG_FindDB_ByOpenHandle	DLPARG_BASE+1
#define DLPARG_FindDB_ByTypeCreator	DLPARG_BASE+2

#define DLPARGLEN_FindDB_ByName		4
#define DLPARGLEN_FindDB_ByOpenHandle	2				
#define DLPARGLEN_FindDB_ByTypeCreator	10

#define DLPRET_FindDB_Basic		DLPRET_BASE
#define DLPRET_FindDB_Size		DLPRET_BASE+1

#define DLPRETLEN_FindDB_Basic		54
#define DLPRETLEN_FindDB_Size		24

#define DLPCMD_FindDB_OptFlag_GetAttributes	0x80	/* get database attributes -- this is
							 * an option to allow find operations to skip
							 * returning this data as a performance optimization
							 */

#define DLPCMD_FindDB_OptFlag_GetSize		0x40	/* get record count and data size also -- this is
							 * an option because the operation can take a long
							 * time, which we would rather avoid if it is not needed
							 */
                                                                                                
#define DLPCMD_FindDB_OptFlag_GetMaxRecSize	0x20	/* get max rec/resource size -- this is
							 * an option because the operation can take a long
							 * time, which we would rather avoid if it is not needed
							 * (dlpFindDBOptFlagGetMaxRecSize is only supported for
							 * dlpFindDBByOpenHandleReqArgID)
							 */

#define DLPCMD_FindDB_SrchFlag_NewSearch	0x80	/* set to beging a new search */
#define DLPCMD_FindDB_SrchFlag_OnlyLatest	0x40	/* set to search for the latest version */



struct dlp_finddb_bytypecreator_req
{
	ubyte	optflags;		/* DLPCMD_FindDB_OptFlag_.... */
	ubyte	srchflags;		/* DLPCMD_FindDB_SrchFlag_.... */

	udword	type;			/* type id (zero = wildcard) */
	udword	creator;		/* creator id (zero = wildcard) */
};

struct dlp_finddb
{
	ubyte	cardno;			/* card number of the database */
	udword	localid;		/* local id of the database */
	udword	openref;		/* db open ref of the database (if currently opened by the caller) */
	
	struct dlp_dbinfo dbinfo;	/* database info */
};			



/** SetDBInfo **/
/* XXX */

/** ExpCardInfo **/
#define DLPARG_ExpCardInfo_Req		DLPARG_BASE
#define DLPARGLEN_ExpCardInfo_Req	2

#define DLPRET_ExpCardInfo_Info		DLPRET_BASE
#define DLPRETLEN_ExpCardInfo_Info	6

struct dlp_expcardinfo
{
	udword	capabilities;
	uword	nstrings;

	char	*strings;
};


#ifdef __cplusplus
extern "C" {
#endif	/* __cplusplus */

/* DLP command functions */
extern int DlpReadUserInfo(
	PConnection *pconn,
	struct dlp_userinfo *userinfo);
extern int DlpWriteUserInfo(
	PConnection *pconn,
	const struct dlp_setuserinfo *userinfo);
extern int DlpReadSysInfo(
	PConnection *pconn,
	struct dlp_sysinfo *sysinfo);
extern int DlpGetSysDateTime(
	PConnection *pconn,
	struct dlp_time *ptime);
extern int DlpSetSysDateTime(
	PConnection *pconn,
	const struct dlp_time *ptime);
extern int DlpReadStorageInfo(		/* XXX - API */
	PConnection *pconn,
	const ubyte card,
	ubyte *last_card,
	ubyte *more,
	struct dlp_cardinfo *cinfo);
extern int DlpReadDBList(
	PConnection *pconn,
	const ubyte iflags,
	const int card,
	const uword start,
	uword *last_index,
	ubyte *oflags,
	ubyte *num,
	struct dlp_dbinfo *dbs);
extern int DlpOpenDB(
	PConnection *pconn,
	int card,
	const char *name,
	ubyte mode,
	ubyte *dbhandle);
extern int DlpCreateDB(
	PConnection *pconn,
	const struct dlp_createdbreq *newdb,
	ubyte *dbhandle);
extern int DlpCloseDB(
	PConnection *pconn,
	ubyte dbhandle,
	ubyte flags);
	/* XXX - This should probably be separated into two functions:
	 * DlpCloseDB(handle) and DlpCloseAllDBs()
	 */
extern int DlpDeleteDB(
	PConnection *pconn,
	const int card,
	const char *name);
extern int DlpReadAppBlock(
	PConnection *pconn,
	const ubyte handle,
	const uword offset,
	const uword len,
	uword *size,
	const ubyte **data);
extern int DlpWriteAppBlock(
	PConnection *pconn,
	const ubyte handle,
	const uword len,
	const ubyte *data);
extern int DlpReadSortBlock(
	PConnection *pconn,
	const ubyte handle,
	const uword offset,
	const uword len,
	uword *size,
	const ubyte **data);
extern int DlpWriteSortBlock(
	PConnection *pconn,
	const ubyte handle,
	const uword len,
	const ubyte *data);
extern int DlpReadNextModifiedRec(
	PConnection *pconn,
	const ubyte handle,
	struct dlp_recinfo *recinfo,
	const ubyte **data);
/* These next two functions both use ReadRecord */
extern int DlpReadRecordByID(
	PConnection *pconn,
	const ubyte handle,
	const udword id,
	const uword offset,
	const uword len,
	struct dlp_recinfo *recinfo,
	const ubyte **data);
extern int DlpReadRecordByIndex(
	PConnection *pconn,
	const ubyte handle,
	const uword index,
	struct dlp_recinfo *recinfo);
/* XXX - Should this use dlp_recinfo instead of explicit values, for
 * symmetry? */
extern int DlpWriteRecord(
	PConnection *pconn,
	const ubyte handle,
	const ubyte flags,
	const udword id,
	const ubyte attributes,
	const ubyte category,
	const udword len,
	const ubyte *data,
	udword *recid);
extern int DlpDeleteRecord(
	PConnection *pconn,
	const ubyte handle,
	const ubyte flags,
	const udword recid);
/* XXX - It's also possible to delete all records, or all records in a
 * category. Add convenience functions for this.
 */
extern int DlpReadResourceByIndex(
	PConnection *pconn,
	const ubyte handle,
	const uword index,
	const uword offset,
	const uword len,
	struct dlp_resource *value,
	const ubyte **data);
extern int DlpReadResourceByType(
	PConnection *pconn,
	const ubyte handle,
	const udword type,
	const uword id,
	const uword offset,
	const uword len,
	struct dlp_resource *value,
	ubyte *data);
/* XXX - Should this use dlp_resource, for symmetry? */
extern int DlpWriteResource(
	PConnection *pconn,
	const ubyte handle,
	const udword type,
	const uword id,
	const uword size,
	const ubyte *data);
extern int DlpDeleteResource(
	PConnection *pconn,
	const ubyte handle,
	const ubyte flags,
	const udword type,
	const uword id);
extern int DlpCleanUpDatabase(
	PConnection *pconn,
	const ubyte handle);
extern int DlpResetSyncFlags(
	PConnection *pconn,
	const ubyte handle);
/* XXX - DlpCallApplication: untested */
extern int DlpCallApplication(
	PConnection *pconn,
	const udword version,
	const struct dlp_appcall *appcall,
	const udword paramsize,
	const ubyte *param,
	struct dlp_appresult *appresult);
extern int DlpResetSystem(
	PConnection *pconn);
extern int DlpAddSyncLogEntry(
	PConnection *pconn,
	const char *msg);
extern int DlpReadOpenDBInfo(
	PConnection *pconn,
	ubyte handle,
	struct dlp_opendbinfo *dbinfo);
extern int DlpMoveCategory(
	PConnection *pconn,
	const ubyte handle,
	const ubyte from,
	const ubyte to);
/* XXX - DlpProcessRPC */
extern int DlpOpenConduit(
	PConnection *pconn);
extern int DlpEndOfSync(
	PConnection *pconn,
	const ubyte status);
extern int DlpResetRecordIndex(
	PConnection *pconn,
	const ubyte handle);
extern int DlpReadRecordIDList(
	PConnection *pconn,
	const ubyte handle,
	const ubyte flags,
	const uword start,
	const uword max,			/* Or DLPC_RRECIDL_TOEND */
	uword *numread,
	udword recids[]);
/* v1.1 functions */
extern int DlpReadNextRecInCategory(
	PConnection *pconn,
	const ubyte handle,
	const ubyte category,
	struct dlp_recinfo *recinfo,
	const ubyte **data);
extern int DlpReadNextModifiedRecInCategory(
	PConnection *pconn,
	const ubyte handle,
	const ubyte category,
	struct dlp_recinfo *recinfo,
	const ubyte **data);
extern int DlpReadAppPreference(
	PConnection *pconn,
	const udword creator,
	const uword id,
	const uword len,			/* Or DLPC_READAPP_FULL */
	const ubyte flags,
	struct dlp_apppref *pref,
	ubyte *data);
/* XXX - DlpWriteAppPreference: untested */
extern int DlpWriteAppPreference(
	PConnection *pconn,
	const udword creator,
	const uword id,
	const ubyte flags,
	const struct dlp_apppref *pref,
	const ubyte *data);
extern int DlpReadNetSyncInfo(
	PConnection *pconn,
	struct dlp_netsyncinfo *netsyncinfo);
extern int DlpWriteNetSyncInfo(
	PConnection *pconn,
	const ubyte modflags,
	const struct dlp_netsyncinfo *newinfo);
extern int DlpReadFeature(
	PConnection *pconn,
	const udword creator,
	const word featurenum,
	udword *value);
extern int DlpFindDB_ByTypeCreator(
	PConnection *pconn,
	struct dlp_finddb *finddbinfo,
	const udword creator,
	const udword type,
	const ubyte newsearch);
extern int DlpExpCardInfo(
	PConnection *pconn,
	const uword slotnum,
	struct dlp_expcardinfo *info);
		                
#ifdef  __cplusplus
}
#endif	/* __cplusplus */

#endif	/* _dlp_cmd_h_ */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
