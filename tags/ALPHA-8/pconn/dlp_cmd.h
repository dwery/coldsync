/* dlp_cmd.h
 *
 * Definitions and types for the DLP convenience functions.
 *
 * $Id: dlp_cmd.h,v 1.10 1999-07-04 02:55:02 arensb Exp $
 */
#ifndef _dlp_cmd_h_
#define _dlp_cmd_h_

#include <palm/palm_types.h>
#include <pconn/PConnection.h>

/* XXX - Reorganize this file into sections, one per command.
 */

/* DLP command codes */
/* 1.0 functions */
#define DLPCMD_ReadUserInfo		0x10	/* Get user info */
#define DLPCMD_WriteUserInfo		0x11	/* Set user info */
#define DLPCMD_ReadSysInfo		0x12	/* Get system info */
#define DLPCMD_GetSysDateTime		0x13	/* Get the time on the Palm */
#define DLPCMD_SetSysDateTime		0x14	/* Set time on the Palm */
#define DLPCMD_ReadStorageInfo		0x15	/* Get memory info */
#define DLPCMD_ReadDBList		0x16	/* Read database list */
#define DLPCMD_OpenDB			0x17	/* Open a database */
#define DLPCMD_CreateDB			0x18	/* Create a new database */
#define DLPCMD_CloseDB			0x19	/* Close database(s) */
#define DLPCMD_DeleteDB			0x1a	/* Delete a database */
#define DLPCMD_ReadAppBlock		0x1b	/* Read AppInfo block */
#define DLPCMD_WriteAppBlock		0x1c	/* Write AppInfo block */
#define DLPCMD_ReadSortBlock		0x1d	/* Read app sort block */
#define DLPCMD_WriteSortBlock		0x1e	/* Write app sort block */
#define DLPCMD_ReadNextModifiedRec	0x1f	/* Read next modified record */
#define DLPCMD_ReadRecord		0x20	/* Read a record */
#define DLPCMD_WriteRecord		0x21	/* Write a record */
#define DLPCMD_DeleteRecord		0x22	/* Delete records */
#define DLPCMD_ReadResource		0x23	/* Read a resource */
#define DLPCMD_WriteResource		0x24	/* Write a resource */
#define DLPCMD_DeleteResource		0x25	/* Delete a resource */
#define DLPCMD_CleanUpDatabase		0x26	/* Purge deleted records */
#define DLPCMD_ResetSyncFlags		0x27	/* Reset dirty flags */
#define DLPCMD_CallApplication		0x28	/* Call an application */
#define DLPCMD_ResetSystem		0x29	/* Reset at end of sync */
#define DLPCMD_AddSyncLogEntry		0x2a	/* Write the sync log */
#define DLPCMD_ReadOpenDBInfo		0x2b	/* Get info about an open DB */
#define DLPCMD_MoveCategory		0x2c	/* Move records in a
						 * category */
#define DLPCMD_ProcessRPC		0x2d	/* Remote Procedure Call */
						/* XXX - Unimplemented:
						 * this looks very cool,
						 * but I don't know where
						 * to begin.
						 */
#define DLPCMD_OpenConduit		0x2e	/* Say a conduit is open */
#define DLPCMD_EndOfSync		0x2f	/* Terminate the sync */
#define DLPCMD_ResetRecordIndex		0x30	/* Reset "modified" index */
#define DLPCMD_ReadRecordIDList		0x31	/* Get list of record IDs */
/* DLP 1.1 functions */
#define DLPCMD_ReadNextRecInCategory	0x32	/* Next record in category */
#define DLPCMD_ReadNextModifiedRecInCategory	0x33
						/* Next modified record in
						 * category */
#define DLPCMD_ReadAppPreference	0x34	/* Read app preference */
#define DLPCMD_WriteAppPreference	0x35	/* Write app preference */
#define DLPCMD_ReadNetSyncInfo		0x36	/* Read NetSync info */
#define DLPCMD_WriteNetSyncInfo		0x37	/* Write NetSync info */
#define DLPCMD_ReadFeature		0x38	/* Read a feature */

/* DLP 1.2 functions (PalmOS v3.0) */
#define DLPCMD_FindDB			0x39
#define DLPCMD_SetDBInfo		0x3a

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
	udword viewerid;	/* ID assigned to viewer by desktop (?) */
			/* XXX - I have no idea what the viewer is, nor
			 * does anyone else, as far as I can tell. Perhaps
			 * this field can be reused for some other purpose.
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
				 * changed */
	ubyte usernamelen;	/* Length of username, including NUL */
				/* XXX - Probably better to determine from
				 * string */
	char *username;		/* User name */
				/* XXX - Should this be an array? */
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
#define DLPRET_ReadSysInfo_Info		DLPRET_BASE
#define DLPRETLEN_ReadSysInfo_Info	14

#define DLPRET_ReadSysInfo_Ver		DLPRET_BASE+1	/* v1.2 extension */
#define DLPRETLEN_ReadSysInfo_Ver	12

/** GetSysDateTime **/
#define DLPRET_GetSysDateTime_Time	DLPRET_BASE
#define DLPRETLEN_GetSysDateTime_Time	8

/** SetSysDateTime **/
#define DLPARG_SetSysDateTime_Time	DLPARG_BASE
#define DLPARGLEN_SetSysDateTime_Time	8

/** ReadStorageInfo **/
#define DLPARG_ReadStorageInfo_Req	DLPARG_BASE
#define DLPARGLEN_ReadStorageInfo_Req	2

/* XXX - Sizes. Get a clue */
#define DLPRET_ReadStorageInfo_Info	DLPRET_BASE
#define DLPRET_ReadStorageInfo_Ext	DLPRET_BASE+1	/* v1.1 */

/** ReadDBList **/
#define DLPARG_ReadDBList_Req		DLPARG_BASE
#define DLPARGLEN_ReadDBList_Req	4

#define DLPRET_ReadDBList_Info		DLPRET_BASE
#define DLPRETLEN_ReadDBList_Info	4

/* Flags for ReadDBList */
#define DLPCMD_READDBLFLAG_RAM		0x80
#define DLPCMD_READDBLFLAG_ROM		0x40
#define DLPCMD_READDBLFLAG_MULT		0x20		/* v1.2 */

#define DLPRET_READDBLFLAG_MORE		0x80

/** OpenDB **/
#define DLPARG_OpenDB_DB		DLPARG_BASE
#define DLPARGLEN_OpenDB_DB		2

#define DLPRET_OpenDB_DB		DLPRET_BASE
#define DLPRETLEN_OpenDB_DB		1

/* Flags for OpenDB */
#define DLPCMD_OPENDBFLAG_READ		0x80
#define DLPCMD_OPENDBFLAG_WRITE		0x40
#define DLPCMD_OPENDBFLAG_EXCL		0x20
#define DLPCMD_OPENDBFLAG_SHOWSECRET	0x10

/** CreateDB **/
#define DLPARG_CreateDB_DB		DLPARG_BASE
#define DLPARGLEN_CreateDB_DB		14

#define DLPRET_CreateDB_DB		DLPRET_BASE
#define DLPRETLEN_CreateDB_DB		1

/** CloseDB **/
#define DLPARG_CloseDB_One		DLPARG_BASE
#define DLPARGLEN_CloseDB_One		1
#define DLPARG_CloseDB_All		DLPARG_BASE+1
#define DLPARGLEN_CloseDB_All		0
#define DLPARG_CloseDB_Update		DLPARG_BASE+2	/* XXX - PalmOS 3.0 */
#define DLPARGLEN_CloseDB_Update	2

/* XXX - Flags */

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

/** WriteRecord **/
#define DLPARG_WriteRecord_Rec		DLPARG_BASE
#define DLPARGLEN_WriteRecord_Rec	8

#define DLPRET_WriteRecord_Rec		DLPRET_BASE
#define DLPRETLEN_WriteRecord_Rec	4

/** DeleteRecord **/
#define DLPARG_DeleteRecord_Rec		DLPARG_BASE
#define DLPARGLEN_DeleteRecord_Rec	6

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

/** WriteResource **/
#define DLPARG_WriteResource_Rsrc	DLPARG_BASE
#define DLPARGLEN_WriteResource_Rsrc	10

/** DeleteResource **/
#define DLPARG_DeleteResource_Res	DLPARG_BASE
#define DLPARGLEN_DeleteResource_Res	8

/** CleanUpDatabase **/
#define DLPARG_CleanUpDatabase_DB	DLPARG_BASE
#define DLPARGLEN_CleanUpDatabase_DB	1

/** ResetSyncFlags **/
#define DLPARG_ResetSyncFlags_DB	DLPARG_BASE
#define DLPARGLEN_ResetSyncFlags_DB	1

/** CallApplication **/
#define DLPARG_CallApplication_V1	DLPARG_BASE
#define DLPARG_CallApplication_V2	DLPARG_BASE+1

#define DLPRET_CallApplication_V1	DLPRET_BASE
#define DLPRETLEN_CallApplication_V1	6
#define DLPRET_CallApplication_V2	DLPRET_BASE+1
#define DLPRETLEN_CallApplication_V2	16

/** ResetSystem **/
/* No arguments, nothing returned */

/** AddSyncLogEntry **/
#define DLPARG_AddSyncLogEntry_Msg	DLPARG_BASE

/** ReadOpenDBInfo **/
#define DLPARG_ReadOpenDBInfo_DB	DLPARG_BASE
#define DLPARGLEN_ReadOpenDBInfo_DB	1

#define DLPRET_ReadOpenDBInfo_Info	DLPRET_BASE
#define DLPRETLEN_ReadOpenDBInfo_Info	2

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

/** ResetRecordIndex **/
#define DLPARG_ResetRecordIndex_DB	DLPARG_BASE
#define DLPARGLEN_ResetRecordIndex_DB	1

/** ReadRecordIDList **/
#define DLPARG_ReadRecordIDList_Req	DLPARG_BASE
#define DLPARGLEN_ReadRecordIDList_Req	6

#define DLPRET_ReadRecordIDList_List	DLPRET_BASE
#define DLPRETLEN_ReadRecordIDList_List	8

#define DLPC_RRECIDL_TOEND		(uword) ~0	/* Read all of the
							 * record IDs.
							 */

/* XXX - Flag: 0x80 -> ask the creator application to sort the database
 * before returning the list.
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

/** WriteAppPreference **/
#define DLPARG_WriteAppPreference_Pref	DLPARG_BASE
#define DLPARGLEN_WriteAppPreference_Pref	12

/** ReadNetSyncInfo **/
#define DLPRET_ReadNetSyncInfo_Info	DLPRET_BASE
#define DLPRETLEN_ReadNetSyncInfo_Info	24

/** WriteNetSyncInfo **/
#define DLPARG_WriteNetSyncInfo_Info	DLPARG_BASE
#define DLPARGLEN_WriteNetSyncInfo_Info	24

/* XXX - Flags */

/** ReadFeature **/
#define DLPARG_ReadFeature_Req		DLPARG_BASE
#define DLPARGLEN_ReadFeature_Req	6

#define DLPRET_ReadFeature_Feature	DLPRET_BASE
#define DLPRETLEN_ReadFeature_Feature	4

/* 1.2 functions */
/** FindDB **/
/* XXX */

/** SetDBInfo **/
/* XXX */

/* XXX - Finish cleaning this up */

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
		/* XXX - The headers hint that the product ID used to be a
		 * variable-sized field. Perhaps this should be supported,
		 * for compatibility with older devices.
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

#define DLPCMD_DBLISTFLAG_MORE	0x80	/* There are more databases to list */

#define DLPCMD_DBNAME_LEN	32	/* 31 chars + NUL (from
					 * dmDBNameLength)
					 */

struct dlp_dbinfo
{
	ubyte size;		/* Total size of the DB info */
	ubyte misc_flags;	/* v1.1 flags */
	uword db_flags;		/* Database flags */
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
#define DLPCMD_DBINFO_LEN	44

#define DBINFO_ISRSRC(dbinfo)	((dbinfo)->db_flags & DLPCMD_DBFLAG_RESDB)
#define DBINFO_ISROM(dbinfo)	((dbinfo)->db_flags & DLPCMD_DBFLAG_RO)

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

/* Sync termination codes */
#define DLPCMD_SYNCEND_NORMAL	0	/* Normal */
#define DLPCMD_SYNCEND_NOMEM	1	/* Out of memory */
#define DLPCMD_SYNCEND_CANCEL	2	/* User cancelled */
#define DLPCMD_SYNCEND_OTHER	3	/* None of the above */

/*** OpenDB ***/
/* Modes for opening databases */
#define DLPCMD_MODE_SECRET	0x10	/* Show secret (?) */
#define DLPCMD_MODE_EXCLUSIVE	0x20	/* ??? */
#define DLPCMD_MODE_WRITE	0x40	/* Writing */
#define DLPCMD_MODE_READ	0x80	/* Reading */

/*** CreateDB ***/
/* Request to create a new database */
struct dlp_createdbreq
{
	udword creator;		/* Database creator */
	udword type;		/* Database type */
	ubyte card;		/* Memory card number */
	ubyte unused;		/* Set to 0 */
	uword flags;		/* Flags. See DLPCMD_DBFLAG_* */
	uword version;		/* Database version */
	char name[128];		/* Database name */
				/* XXX - Fixed size: bad. Find out what
				 * maximum length is. I suspect it's 31
				 * chars + NUL, from 'dmDBNameLength' in
				 * DataMgr.h */
};

/*** CloseDB ***/
#define DLPCMD_CLOSEALLDBS	0xff	/* Close all databases */

/*** ReadRecord ***/
struct dlp_recinfo
{
	udword id;		/* Record ID */
	uword index;		/* Record index */
	uword size;		/* Record size */
	ubyte attributes;	/* Record attributes */
	ubyte category;		/* Record category */
};

/* XXX - Get rid of this */
struct dlp_readrecret
{
	udword recid;		/* Unique record ID */
	uword index;		/* Record index */
	uword size;		/* Record size, in bytes */
	ubyte attributes;	/* Record attributes (?) */
	ubyte category;		/* Record category index */
	const void *data;	/* Record data */
};

/*** WriteRecord ***/

/*** DeleteRecord ***/
#define DLPCMD_DELRECFLAG_ALL		0x80
					/* Ignore the record ID. Delete all
					 * records */
#define DLPCMD_DELRECFLAG_CATEGORY	0x40
					/* The least significant byte of
					 * the record ID contains the
					 * category of records to be
					 * deleted.
					 */

/*** CallApplication ***/
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
				/* XXX - Fixed size: bad! */
};

/*** ReadResource ***/
struct dlp_resource
{
	udword type;		/* Resource type */
	uword id;		/* Resource ID */
	uword index;		/* Resource index */
	uword size;		/* Resource size */
};

/*** ReadOpenDBInfo ***/
struct dlp_opendbinfo
{
	/* Yes, it seems bogus to have a struct that contains a single
	 * field, but with a name like ReadOpenDBInfo, this just seems like
	 * the sort of function that might return more information in the
	 * future.
	 */
	uword numrecs;		/* # records in database */
};

/*** ReadAppPreference ***/
struct dlp_apppref
{
	uword version;		/* Version number */
				/* XXX - App or preference? */
	uword size;		/* Actual size of preference */
	uword len;		/* # bytes returned by
				 * DlpReadAppPreference() */
};

/* XXX - Need flags */

/*** ReadNetSyncInfo ***/
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
	udword reserved1b;	/* Set to 0 */	/* XXX - bogus name */
	udword reserved2;	/* Set to 0 */
	udword reserved3;	/* Set to 0 */
	udword reserved4;	/* Set to 0 */
		/* For some reason, the hostname, address and netmask
		 * are not stored as binary data (4 bytes for the IP
		 * address), but rather as NUL-terminated strings.
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

/*** WriteNetSyncInfo ***/
struct dlp_writenetsyncinfo
{
	ubyte modflags;		/* Indicates which fields were modified */

	/* XXX - This is clumsy. Might it be better to combine
	 * dlp_netsyncinfo and dlp_writenetsyncinfo into one structure? Or
	 * add a 'modflags' argument to DlpWriteNetSyncInfo(), specifying
	 * which fields need to be looked at?
	 */
	struct dlp_netsyncinfo netsyncinfo;
				/* The NetSync information to write */
};

/* Flags to indicate which values are being changed by this
 * WriteNetSyncInfo request.
 */
#define DLPCMD_MODNSFLAG_LANSYNC	0x80	/* LAN sync setting */
#define DLPCMD_MODNSFLAG_HOSTNAME	0x40	/* Sync host name */
#define DLPCMD_MODNSFLAG_HOSTADDR	0x20	/* Sync host address */
#define DLPCMD_MODNSFLAG_NETMASK	0x10	/* Sync host netmask */

#ifdef __cplusplus
extern "C" {
#endif	/* __cplusplus */

/* DLP command functions */
extern int DlpReadUserInfo(
	struct PConnection *pconn,
	struct dlp_userinfo *userinfo);
extern int DlpWriteUserInfo(
	struct PConnection *pconn,
	const struct dlp_setuserinfo *userinfo);
extern int DlpReadSysInfo(
	struct PConnection *pconn,
	struct dlp_sysinfo *sysinfo);
extern int DlpGetSysDateTime(
	struct PConnection *pconn,
	struct dlp_time *ptime);
extern int DlpSetSysDateTime(
	struct PConnection *pconn,
	const struct dlp_time *ptime);
extern int DlpReadStorageInfo(		/* XXX - API */
	struct PConnection *pconn,
	const ubyte card,
	ubyte *last_card,
	ubyte *more,
	struct dlp_cardinfo *cinfo);
extern int DlpReadDBList(
	struct PConnection *pconn,
	const ubyte iflags,
	const int card,
	const uword start,
	uword *last_index,
	ubyte *oflags,
	ubyte *num,
	struct dlp_dbinfo *dbs);
extern int DlpOpenDB(
	struct PConnection *pconn,
	int card,
	const char *name,
	ubyte mode,
	ubyte *dbhandle);
extern int DlpCreateDB(
	struct PConnection *pconn,
	const struct dlp_createdbreq *newdb,
	ubyte *dbhandle);
extern int DlpCloseDB(
	struct PConnection *pconn,
	const ubyte dbhandle);
	/* XXX - This should probably be separated into two functions:
	 * DlpCloseDB(handle) and DlpCloseAllDBs()
	 */
extern int DlpDeleteDB(
	struct PConnection *pconn,
	const int card,
	const char *name);
extern int DlpReadAppBlock(
	struct PConnection *pconn,
	const ubyte handle,
	const uword offset,
	const uword len,
	uword *size,
	const ubyte **data);
extern int DlpWriteAppBlock(
	struct PConnection *pconn,
	const ubyte handle,
	const uword offset,
	const uword len,
	const ubyte *data);
extern int DlpReadSortBlock(
	struct PConnection *pconn,
	const ubyte handle,
	const uword offset,
	const uword len,
	uword *size,
	const ubyte **data);
extern int DlpWriteSortBlock(
	struct PConnection *pconn,
	const ubyte handle,
	const uword offset,
	const uword len,
	const ubyte *data);
extern int DlpReadNextModifiedRec(	/* XXX - Bogus API */
	struct PConnection *pconn,
	const ubyte handle,
	struct dlp_recinfo *recinfo,
	const ubyte **data);
/* These next two functions both use ReadRecord */
extern int DlpReadRecordByID(
	struct PConnection *pconn,
	const ubyte handle,
	const udword id,
	const uword offset,
	const uword len,
	struct dlp_recinfo *recinfo,
	const ubyte **data);
extern int DlpReadRecordByIndex(
	struct PConnection *pconn,
	const ubyte handle,
	const uword index,
	struct dlp_recinfo *recinfo);
extern int DlpWriteRecord(
	struct PConnection *pconn,
	const ubyte handle,
	const ubyte flags,
	const udword id,
	const ubyte attributes,
	const ubyte category,
	const udword len,
	const ubyte *data,
	udword *recid);
extern int DlpDeleteRecord(
	struct PConnection *pconn,
	const ubyte handle,
	const ubyte flags,
	const udword recid);
extern int DlpReadResourceByIndex(
	struct PConnection *pconn,
	const ubyte handle,
	const uword index,
	const uword offset,
	const uword len,
	struct dlp_resource *value,
	const ubyte **data);
extern int DlpReadResourceByType(
	struct PConnection *pconn,
	const ubyte handle,
	const udword type,
	const uword id,
	const uword offset,
	const uword len,
	struct dlp_resource *value,
	ubyte *data);
extern int DlpWriteResource(
	struct PConnection *pconn,
	const ubyte handle,
	const udword type,
	const uword id,
	const uword size,
	const ubyte *data);
extern int DlpDeleteResource(
	struct PConnection *pconn,
	const ubyte handle,
	const ubyte flags,
	const udword type,
	const uword id);
extern int DlpCleanUpDatabase(
	struct PConnection *pconn,
	const ubyte handle);
extern int DlpResetSyncFlags(
	struct PConnection *pconn,
	const ubyte handle);
/* XXX - DlpCallApplication: untested */
extern int DlpCallApplication(
	struct PConnection *pconn,
	const udword version,
	const struct dlp_appcall *appcall,
	const udword paramsize,
	const ubyte *param,
	struct dlp_appresult *appresult);
extern int DlpResetSystem(
	struct PConnection *pconn);
extern int DlpAddSyncLogEntry(
	struct PConnection *pconn,
	const char *msg);
extern int DlpReadOpenDBInfo(
	struct PConnection *pconn,
	ubyte handle,
	struct dlp_opendbinfo *dbinfo);
extern int DlpMoveCategory(
	struct PConnection *pconn,
	const ubyte handle,
	const ubyte from,
	const ubyte to);
/* XXX - DlpProcessRPC */
extern int DlpOpenConduit(
	struct PConnection *pconn);
extern int DlpEndOfSync(
	struct PConnection *pconn,
	const ubyte status);
extern int DlpResetRecordIndex(
	struct PConnection *pconn,
	const ubyte handle);
extern int DlpReadRecordIDList(
	struct PConnection *pconn,
	const ubyte handle,
	const ubyte flags,
	const uword start,
	const uword max,
	uword *numread,
	udword recids[]);	/* XXX - Should this allocate a list? */
/* v1.1 functions */
extern int DlpReadNextRecInCategory(		/* XXX - bogus API */
	struct PConnection *pconn,
	const ubyte handle,
	const ubyte category,
	struct dlp_readrecret *record);
extern int DlpReadNextModifiedRecInCategory(	/* XXX - bogus API */
	struct PConnection *pconn,
	const ubyte handle,
	const ubyte category,
	struct dlp_readrecret *record);
/* XXX - DlpReadAppPreference: untested */
extern int DlpReadAppPreference(
	struct PConnection *pconn,
	const udword creator,
	const uword id,
	const uword len,
	const ubyte flags,
	struct dlp_apppref *pref,
	ubyte *data);
/* XXX - DlpWriteAppPreference: untested */
extern int DlpWriteAppPreference(
	struct PConnection *pconn,
	const udword creator,
	const uword id,
	const ubyte flags,
	const struct dlp_apppref *pref,
	const ubyte *data);
extern int DlpReadNetSyncInfo(
	struct PConnection *pconn,
	struct dlp_netsyncinfo *netsyncinfo);
extern int DlpWriteNetSyncInfo(
	struct PConnection *pconn,
	const struct dlp_writenetsyncinfo *netsyncinfo);
extern int DlpReadFeature(
	struct PConnection *pconn,
	const udword creator,
	const word featurenum,
	udword *value);

#ifdef  __cplusplus
}
#endif	/* __cplusplus */

#endif	/* _dlp_cmd_h_ */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
