/* dlp_cmd.h
 *
 * Definitions and types for the DLP convenience functions.
 *
 * $Id: dlp_cmd.h,v 1.1 1999-02-19 22:51:54 arensb Exp $
 */
#ifndef _dlp_cmd_h_
#define _dlp_cmd_h_
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
#define DLPCMD_ReadDBList		0x16	/* XXX - Read database list */
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
#define DLPCMD_WriteRecord		0x21	/* XXX - Write a record */
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
/* 1.1 functions */
#define DLPCMD_ReadNextRecInCategory	0x32	/* Next record in category */
#define DLPCMD_ReadNextModifiedRecInCategory	0x33
						/* Next modified record in
						 * category */
#define DLPCMD_ReadAppPreference	0x34	/* Read app preference */
#define DLPCMD_WriteAppPreference	0x35	/* Write app preference */
#define DLPCMD_ReadNetSyncInfo		0x36	/* Read NetSync info */
#define DLPCMD_WriteNetSyncInfo		0x37	/* Write NetSync info */
#define DLPCMD_ReadFeature		0x38	/* Read a feature */

/* 1.2 functions (PalmOS v3.0) */
#define DLPCMD_FindDB			0x39
#define DLPCMD_SetDBInfo		0x3a

/* Argument IDs, by command */
#define DLPARG_BASE			0x20

#define DLPARG_WriteUserInfo_UserInfo	DLPARG_BASE
#define DLPARGLEN_WriteUserInfo_UserInfo	22
						/* Doesn't include
						 * user name */
#define DLPARG_SetSysDateTime_Time	DLPARG_BASE
#define DLPARGLEN_SetSysDateTime_Time	8

#define DLPARG_OpenDB_DB		DLPARG_BASE

#define DLPARG_CreateDB_DB		DLPARG_BASE

#define DLPARG_CloseDB_One		DLPARG_BASE
#define DLPARGLEN_CloseDB_One		1
#define DLPARG_CloseDB_All		DLPARG_BASE+1
#define DLPARGLEN_CloseDB_All		0

#define DLPARG_DeleteDB_DB		DLPARG_BASE
#define DLPARGLEN_DeleteDB_DB		2

#define DLPARG_ReadAppBlock_Req		DLPARG_BASE
#define	DLPARGLEN_ReadAppBlock_Req	6

#define DLPARG_WriteAppBlock_Block	DLPARG_BASE
#define DLPARGLEN_WriteAppBlock_Block	4

#define DLPARG_ReadSortBlock_Req	DLPARG_BASE
#define	DLPARGLEN_ReadSortBlock_Req	6

#define DLPARG_WriteSortBlock_Block	DLPARG_BASE
#define DLPARGLEN_WriteSortBlock_Block	4

#define DLPARG_ReadNextModifiedRec_Req	DLPARG_BASE
#define DLPARGLEN_ReadNextModifiedRec_Req	1

#define DLPARG_ReadAppPreference_Pref	DLPARG_BASE
#define DLPARGLEN_ReadAppPreference_Pref	10

#define DLPARG_WriteAppPreference_Pref	DLPARG_BASE
#define DLPARGLEN_WriteAppPreference_Pref	12

#define DLPARG_ReadRecord_ByID		DLPARG_BASE
#define DLPARGLEN_ReadRecord_ByID	10
#define DLPARG_ReadRecord_ByIndex	DLPARG_BASE+1
#define DLPARGLEN_ReadRecord_ByIndex	8

#define DLPARG_WriteRecord_Rec		DLPARG_BASE
#define DLPARGLEN_WriteRecord_Rec	8

#define DLPARG_DeleteRecord_Rec		DLPARG_BASE
#define DLPARGLEN_DeleteRecord_Rec	6

#define DLPARG_ReadResource_ByIndex	DLPARG_BASE
#define DLPARGLEN_ReadResource_ByIndex	8
#define DLPARG_ReadResource_ByType	DLPARG_BASE+1
#define DLPARGLEN_ReadResource_ByType	12

#define DLPARG_WriteResource_Res	DLPARG_BASE
#define DLPARGLEN_WriteResource_Res	10

#define DLPARG_DeleteResource_Res	DLPARG_BASE
#define DLPARGLEN_DeleteResource_Res	8

#define DLPARG_CleanUpDatabase_DB	DLPARG_BASE
#define DLPARGLEN_CleanUpDatabase_DB	1

#define DLPARG_ResetSyncFlags_DB	DLPARG_BASE
#define DLPARGLEN_ResetSyncFlags_DB	1

#define DLPARG_CallApplication_V1	DLPARG_BASE
#define DLPARG_CallApplication_V2	DLPARG_BASE+1
/* Possible result codes */
#define DLPRET_CallApplication_V1	DLPARG_BASE
#define DLPRET_CallApplication_V2	DLPARG_BASE+1

#define DLPARG_AddSyncLogEntry_Msg	DLPARG_BASE

#define DLPARG_ReadOpenDBInfo_DB	DLPARG_BASE
#define DLPARGLEN_ReadOpenDBInfo_DB	1

#define DLPARG_MoveCategory_Cat		DLPARG_BASE
#define DLPARGLEN_MoveCategory_Cat	4

#define DLPARG_EndOfSync_Status		DLPARG_BASE
#define DLPARGLEN_EndOfSync_Status	2

#define DLPARG_ResetRecordIndex_DB	DLPARG_BASE
#define DLPARGLEN_ResetRecordIndex_DB	1

#define DLPARG_ReadRecordIDList_Req	DLPARG_BASE
#define DLPARGLEN_ReadRecordIDList_Req	6

#define DLPARG_ReadNextRecInCategory_Rec	DLPARG_BASE
#define DLPARGLEN_ReadNextRecInCategory_Rec	2

#define DLPARG_ReadNextModifiedRecInCategory_Rec	DLPARG_BASE
#define DLPARGLEN_ReadNextModifiedRecInCategory_Rec	2

#define DLPARG_WriteNetSyncInfo_Info	DLPARG_BASE
#define DLPARGLEN_WriteNetSyncInfo_Info	24

#define DLPARG_ReadFeature_Req		DLPARG_BASE
#define DLPARGLEN_ReadFeature_Req	6

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
	const unsigned char *cardname;
	const unsigned char *manufname;
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

/* Application (info?) block */
struct dlp_appblock
{
	ubyte dbid;		/* Database ID (handle) */
	ubyte unused;		/* Set to 0 */
	uword offset;		/* Offset into block */
	uword len;		/* # bytes to read/write, starting at 'offset'
				 * (-1 == to the end)
				 */
};

/* Sort block (same as dlp_appblock, actually) */
struct dlp_sortblock
{
	ubyte dbid;		/* Database ID (handle) */
	ubyte unused;		/* Set to 0 */
	uword offset;		/* Offset into block */
	uword len;		/* # bytes to read/write, starting at 'offset'
				 * (-1 == to the end)
				 */
};

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
/* XXX - These identifiers need work */
struct dlp_readrecreq_byid
{
	ubyte dbid;		/* Database ID (handle) */
	ubyte unused;		/* Set to 0 */
	udword recid;		/* Record ID */
	uword offset;		/* Offset into the record */
	uword len;		/* How many bytes to read, starting at
				 * 'offset' (-1 == to the end). */
};

struct dlp_readrecreq_byindex
{
	ubyte dbid;		/* Database ID (handle) */
	ubyte unused;		/* Set to 0 */
	uword index;		/* Record index */
	uword offset;		/* Offset into the record */
	uword len;		/* How many bytes to read, starting at
				 * 'offset' (-1 == to the end). */
};

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
struct dlp_writerec
{
	ubyte dbid;		/* Database ID (handle) */
	ubyte flags;		/* Record flags */
	udword recid;		/* Unique record ID */
	ubyte attributes;	/* Record attributes */
	ubyte category;		/* Record category */
	void *data;		/* Record data */
};

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
	uword numrecs;		/* # records in database */
};

/*** ReadRecordIDList ***/
struct dlp_idlistreq
{
	ubyte dbid;		/* Database ID (handle) */
	ubyte flags;		/* Flags (DLPREQ_IDLFLAG_*) */
	uword start;		/* Where to start reading */
	uword max;		/* Max # entries to return */
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
	/* XXX - Remove the 'sync' */
	char synchostname[DLPCMD_MAXHOSTNAMELEN];
				/* Name of sync host */
	char synchostaddr[DLPCMD_MAXADDRLEN];
				/* Address of sync host */
	char synchostnetmask[DLPCMD_MAXNETMASKLEN];
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
extern int DlpReadUserInfo(int fd, struct dlp_userinfo *userinfo);
extern int DlpWriteUserInfo(int fd, const struct dlp_setuserinfo *userinfo);
extern int DlpReadSysInfo(int fd, struct dlp_sysinfo *sysinfo);
extern int DlpGetSysDateTime(int fd, struct dlp_time *ptime);
extern int DlpSetSysDateTime(int fd, const struct dlp_time *ptime);
extern int DlpReadStorageInfo(int fd, int card);	/* XXX - API */
extern int DlpReadDBList(int fd, ubyte flags, int card, uword start); /* XXX - API */
extern int DlpOpenDB(int fd, int card, const char *name, ubyte mode,
		     ubyte *dbhandle);
extern int DlpCreateDB(int fd, const struct dlp_createdbreq *newdb,
		       ubyte *dbhandle);
extern int DlpCloseDB(int fd, const ubyte dbhandle);
	/* XXX - This should probably be separated into two functions:
	 * DlpCloseDB(handle) and DlpCloseAllDBs()
	 */
extern int DlpDeleteDB(int fd, const int card, const char *name);
extern int DlpReadAppBlock(int fd, struct dlp_appblock *appblock,
			   uword *len, ubyte *data);
extern int DlpWriteAppBlock(int fd,
			    const struct dlp_appblock *appblock,
			    ubyte *data);
extern int DlpReadSortBlock(int fd, struct dlp_sortblock *sortblock,
			    uword *len, ubyte *data);
extern int DlpWriteSortBlock(int fd,
			     const struct dlp_sortblock *sortblock,
			     ubyte *data);
extern int DlpReadNextModifiedRec(int fd, const ubyte handle,
				  struct dlp_readrecret *record);
/* These next two functions both use ReadRecord */
extern int DlpReadRecordByID(int fd,
			     const struct dlp_readrecreq_byid *req,
			     struct dlp_readrecret *record);
extern int DlpReadRecordByIndex(int fd,
				const struct dlp_readrecreq_byindex *req,
				struct dlp_readrecret *record);
extern int DlpWriteRecord(int fd, const udword len,
			  const struct dlp_writerec *rec,
			  udword *recid);
extern int DlpDeleteRecord(int fd, const ubyte dbid, const ubyte flags,
			   const udword recid);
extern int DlpReadResourceByIndex(int fd, const ubyte dbid, const uword index,
				  const uword offset, const uword len,
				  struct dlp_resource *value,
				  ubyte *data);
extern int DlpReadResourceByType(int fd, const ubyte dbid, const udword type,
				 const uword id, const uword offset,
				 const uword len,
				 struct dlp_resource *value,
				 ubyte *data);
extern int DlpWriteResource(int fd, const ubyte dbid, const udword type,
			    const uword id, const uword size,
			    const ubyte *data);
extern int DlpDeleteResource(int fd, const ubyte dbid, const ubyte flags,
			     const udword type, const uword id);
extern int DlpCleanUpDatabase(int fd, const ubyte dbid);
extern int DlpResetSyncFlags(int fd, const ubyte dbid);
/* XXX - DlpCallApplication: untested */
extern int DlpCallApplication(int fd, const udword version,
			      const struct dlp_appcall *appcall,
			      const udword paramsize,
			      const ubyte *param,
			      struct dlp_appresult *appresult);
extern int DlpResetSystem(int fd);
extern int DlpAddSyncLogEntry(int fd, const char *msg);
extern int DlpReadOpenDBInfo(int fd, ubyte handle,
			     struct dlp_opendbinfo *dbinfo);
extern int DlpMoveCategory(int fd, const ubyte handle,
			   const ubyte from, const ubyte to);
/* XXX - DlpProcessRPC */
extern int DlpOpenConduit(int fd);
extern int DlpEndOfSync(int fd, const ubyte status);
extern int DlpResetRecordIndex(int fd, const ubyte dbid);
extern int DlpReadRecordIDList(int fd, const struct dlp_idlistreq *idreq,
			       uword *numread,
			       udword recids[]);
/* v1.1 functions */
extern int DlpReadNextRecInCategory(int fd, const ubyte handle,
				    const ubyte category,
				    struct dlp_readrecret *record);
extern int DlpReadNextModifiedRecInCategory(int fd, const ubyte handle,
					    const ubyte category,
					    struct dlp_readrecret *record);
/* XXX - DlpReadAppPreference: untested */
extern int DlpReadAppPreference(int fd, const udword creator, const uword id,
				const uword len, const ubyte flags,
				struct dlp_apppref *pref,
				ubyte *data);
/* XXX - DlpWriteAppPreference: untested */
extern int DlpWriteAppPreference(int fd, const udword creator, const uword id,
				 const ubyte flags,
				 const struct dlp_apppref *pref,
				 const ubyte *data);
extern int DlpReadNetSyncInfo(int fd, struct dlp_netsyncinfo *netsyncinfo);
extern int DlpWriteNetSyncInfo(
	int fd,
	const struct dlp_writenetsyncinfo *netsyncinfo);
extern int DlpReadFeature(int fd, const udword creator, const word featurenum,
			  udword *value);

#ifdef  __cplusplus
}
#endif	/* __cplusplus */

#endif	/* _dlp_cmd_h_ */
