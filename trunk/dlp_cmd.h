/* dlp_cmd.h
 *
 * Definitions and types for the DLP convenience functions.
 *
 * $Id: dlp_cmd.h,v 1.1 1999-01-31 22:25:58 arensb Exp $
 */
#ifndef _dlp_cmd_h_
#define _dlp_cmd_h_

/* DLP command codes */
/* 1.0 functions */
#define DLPCMD_ReadUserInfo		0x10	/* Get user info */
#define DLPCMD_WriteUserInfo		0x11	/* Set user info */
#define DLPCMD_ReadSysInfo		0x12	/* Get system info */
#define DLPCMD_GetSysDateTime		0x13	/* Get the time on the Palm */
#define DLPCMD_SetSysDateTime		0x14	/* Set time on the Palm */
#define DLPCMD_ReadStorageInfo		0x15	/* Get memory info */
#define DLPCMD_ReadDBList		0x16
#define DLPCMD_OpenDB			0x17
#define DLPCMD_CreateDB			0x18
#define DLPCMD_CloseDB			0x19
#define DLPCMD_DeleteDB			0x1a
#define DLPCMD_ReadAppBlock		0x1b
#define DLPCMD_WriteAppBlock		0x1c
#define DLPCMD_ReadSortBlock		0x1d
#define DLPCMD_WriteSortBlock		0x1e
#define DLPCMD_ReadNextModifiedRec	0x1f
#define DLPCMD_ReadRecord		0x20
#define DLPCMD_WriteRecord		0x21
#define DLPCMD_DeleteRecord		0x22
#define DLPCMD_ReadResource		0x23
#define DLPCMD_WriteResource		0x24
#define DLPCMD_DeleteResource		0x25
#define DLPCMD_CleanUpDatabase		0x26
#define DLPCMD_ResetSyncFlags		0x27
#define DLPCMD_CallApplication		0x28
#define DLPCMD_ResetSystem		0x29
#define DLPCMD_AddSyncLogEntry		0x2a	/* Write the sync log */
#define DLPCMD_ReadOpenDBInfo		0x2b
#define DLPCMD_MoveCategory		0x2c
#define DLPCMD_ProcessRPC		0x2d
#define DLPCMD_OpenConduit		0x2e
#define DLPCMD_EndOfSync		0x2f	/* Terminate the sync */
#define DLPCMD_ResetRecordIndex		0x30
#define DLPCMD_ReadRecordIDList		0x31
/* 1.1 functions */
#define DLPCMD_ReadNextRecInCategory	0x32
#define DLPCMD_ReadNextModifiedRecInCategory	0x33
#define DLPCMD_ReadAppPreference	0x34
#define DLPCMD_WriteAppPreference	0x35
#define DLPCMD_ReadNetSyncInfo		0x36
#define DLPCMD_WriteNetSyncInfo		0x37
#define DLPCMD_ReadFeature		0x38

/* Argument IDs, by command */
#define DLPARG_BASE			0x20

#define DLPARG_WriteUserInfo_UserInfo	DLPARG_BASE
#define DLPARGLEN_WriteUserInfo_UserInfo	22
						/* Doesn't include
						 * user name */
#define DLPARG_SetSysDateTime_Time	DLPARG_BASE
#define DLPARGLEN_SetSysDateTime_Time	8

#define DLPARG_AddSyncLogEntry_Msg	0x20

#define DLPARG_EndOfSync_Status		DLPARG_BASE
#define DLPARGLEN_EndOfSync_Status	2

#define DLPCMD_USERNAME_LEN		41	/* Max. length of user name,
						 * including terminating
						 * NUL. */

/* dlp_time
 * Structure for giving date and time.
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

/* dlp_userinfo
 * The data returned from a DlpReadUserInfo command.
 */
struct dlp_userinfo
{
	udword userid;		/* User ID number (0 if none) */
	udword viewerid;	/* ID assigned to viewer by desktop (?) */
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
};

/* dlp_storageinfo
 */
/* XXX - This should probably never be seen by the programmer; hide it
 * in slp.h.
 */
struct dlp_storageinfo
{
	ubyte lastcard;		/* # of last card */
	ubyte more;		/* Non-zero if there are more cards */
	ubyte unused;		/* Set to 0 */
	ubyte act_count;	/* Actual count of structures returned */
};

/* XXX - Should this include the 1.1 extensions? */
struct dlp_cardinfo
{
	ubyte totalsize;	/* Total size of this card info */
	ubyte cardno;		/* Card number */
	uword cardversion;	/* Card version */
	struct dlp_time ctime;	/* Creation date and time */
	udword rom_size;	/* ROM size */
	udword ram_size;	/* RAM size */
	udword free_ram;	/* Free RAM size */
	ubyte cardname_size;	/* Size of card name string */
	ubyte manufname_size;	/* Size of manufacturer name string */
	char *cardname;
	char *manufname;
};

struct dlp_cardinfo_ext		/* 1.1 extension */
{
	uword rom_dbs;		/* ROM database count */
	uword ram_dbs;		/* RAM database count */
	udword reserved1;	/* Set to 0 */
	udword reserved2;	/* Set to 0 */
	udword reserved3;	/* Set to 0 */
	udword reserved4;	/* Set to 0 */
};


/* XXX - This probably belongs in dlp.h */
struct dlp_dblist_header
{
	uword lastindex;	/* Index of last database returned */
	ubyte flags;		/* Flags */
	ubyte act_count;	/* Actual count of structs returned */
};
#define DLPCMD_DBLISTFLAG_MORE	0x80	/* There are more databases to list */

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
};
#define DLPCMD_DBINFO_LEN	44

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

typedef enum {
	dlpExitNormal = 0,	/* Normal termination */
	dlpExitNoMemory = 1,	/* Out of memory */
	dlpExitUserCancel = 2,	/* User cancelled */
	dlpExitOther = 3,	/* None of the above */
} dlpEndOfSyncStatus;	/* XXX - Bogus name */

#ifdef __cplusplus
extern "C" {
#endif	/* __cplusplus */

/* DLP command functions */
extern int DlpReadUserInfo(int fd, struct dlp_userinfo *userinfo);
extern int DlpWriteUserInfo(int fd, struct dlp_setuserinfo *userinfo);
extern int DlpReadSysInfo(int fd, struct dlp_sysinfo *sysinfo);
extern int DlpGetSysDateTime(int fd, struct dlp_time *ptime);
extern int DlpSetSysDateTime(int fd, struct dlp_time *ptime);
extern int DlpReadStorageInfo(int fd, int card);	/* XXX - API */
extern int DlpReadDBList(int fd, ubyte flags, int card, uword start); /* XXX - API */
extern int DlpAddSyncLogEntry(int fd, char *msg);
extern int DlpEndOfSync(int fd, dlpEndOfSyncStatus status);

#ifdef  __cplusplus
}
#endif	/* __cplusplus */

#endif	/* _dlp_cmd_h_ */
