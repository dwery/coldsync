/* pdb.h
 *
 * Definitions and such for Palm databases.
 *
 * $Id: pdb.h,v 1.1 1999-03-11 03:27:51 arensb Exp $
 */
#ifndef _pdb_h_
#define _pdb_h_

#include <palm/palm_types.h>

/* XXX - Add a type (and support functions) for those ubitquitous
 * 4-character IDs.
 */

#define EPOCH_1904	2082844800L	/* Difference, in seconds, between
					 * Palm's epoch (Jan. 1, 1904) and
					 * Unix's epoch (Jan. 1, 1970).
					 */

#define PDB_DBNAMELEN	32		/* Length of name field in database
					 * header */

/* Database attribute flags */
/* XXX - Move these to a common area */
#define PDB_ATTR_RESDB		0x0001	/* This is a resource database.
					 * Resource databases are usually
					 * saved in files with ".prc"
					 * extensions. Other databases are
					 * saved with a ".pdb" extension.
					 */
#define PDB_ATTR_RO		0x0002	/* Read-only database */
#define PDB_ATTR_APPINFODIRTY	0x0004	/* App info block is dirty */
#define PDB_ATTR_BACKUP		0x0008	/* Back up the database if no
					 * app-specific conduit exists */
#define PDB_ATTR_OKNEWER	0x0010	/* Tells the backup conduit that
					 * it's okay to install a newer
					 * version of this database with a
					 * different name if this one is
					 * open. Usually used for the
					 * Graffiti Shortcuts database.
					 */
#define PDB_ATTR_RESET		0x0020	/* Reset the Palm after the
					 * database is installed */
#define PDB_ATTR_OPEN		0x0040	/* Database is open */

/* Record attributes */
#define PDB_REC_DELETE		0x80	/* Delete this record next sync */
#define PDB_REC_ARCHIVE		0x40	/* Archive this record next sync */
#define PDB_REC_BUSY		0x20	/* Record is currently in use */
#define PDB_REC_PRIVATE		0x10	/* Record is private: don't show to
					 * anyone without asking for a
					 * password.
					 */
#define PDB_REC_ARCHIVED	0x08	/* Archived record */
					/* XXX - What does this mean? Is
					 * this something that only gets
					 * set on the desktop?
					 */

typedef udword localID;			/* Local (card-relative) chunk ID */
					/* XXX - What does this mean? */

struct pdb_header
{
	char name[PDB_DBNAMELEN];	/* Database name */
					/* XXX - NUL-terminated? */
	uword attributes;		/* Database attributes */
	uword version;			/* Database version */

	udword ctime;			/* Creation time */
	udword mtime;			/* Time of last modification */
	udword baktime;			/* Time of last backup */
	udword modnum;			/* Modification number */
			/* XXX - What exactly is the modification number?
			 * Does it get incremented each time you, say,
			 * create a new category?
			 */

	/* XXX - These two should probably be called something like
	 * appinfo_offset and sortinfo_offset, since "ID" is misleading.
	 */
	localID appinfoID;		/* App-specific info */
	localID sortinfoID;		/* App-specific sorting info */

	udword type;			/* Database type */
	udword creator;			/* Database creator */

	udword uniqueIDseed;		/* Used to generate unique IDs.
					 * Only the lower 3 bytes are used.
					 * The high byte is for alignment.
					 */
};
#define PDB_HEADER_LEN		72

struct pdb_recordlist_header
{
	localID nextID;			/* ID of next list */
	uword len;			/* # records */
};
#define PDB_RECORDLIST_LEN	6

/* pdb_record
 * A plain old record, containing arbitrary data.
 */
struct pdb_record
{
	localID offset;			/* Offset of record in file */
	ubyte attributes;		/* Record attributes */
/*	ubyte uniqueID[3];*/		/* Record's unique ID. Legal records
					 * do not have an ID of 0. */
	udword uniqueID;		/* Record's unique ID. Actually,
					 * only the bottom 3 bytes are
					 * stored in the file, but for
					 * everything else, it's much
					 * easier to just consider this a
					 * 32-bit integer.
					 */
		/* XXX - This 3-byte array is brain-damaged. Yeah, I know
		 * this is how it comes from the Palm, but for everything
		 * else it's a PITA. Just make it a 'udword' and make
		 * reading and writing a separate case.
		 */
};
#define PDB_RECORDIX_LEN	8	/* Size of a pdb_record in a file */

/* pdb_resource
 * Mac hackers should feel at home here: the type of a resource is really a
 * 4-character category identifier, and the ID is an integer within that
 * category.
 */
struct pdb_resource
{
	udword type;			/* Resource type */
	uword id;			/* Resource ID */
	localID offset;			/* Offset of resource in file */
};
#define PDB_RESOURCEIX_LEN	10	/* Size of a pdb_resource in a file */

/* pdb
 * Structure of a Palm database (file), both resource databases (.prc) and
 * record databases (.pdb).
 */
struct pdb
{
	long file_size;			/* Total length of file */
	struct pdb_header header;	/* Database header */
	struct pdb_recordlist_header reclist_header;
					/* Record list header */
	/* Record list. This is actually an array */
	/* XXX - This probably ought to become a linked list at some point,
	 * to make it easier to merge two databases in memory.
	 */
	union {
		struct pdb_record *rec;
		struct pdb_resource *res;
	} rec_index;
	long appinfo_len;		/* Length of AppInfo block */
	void *appinfo;			/* Optional AppInfo block */
	long sortinfo_len;		/* Length of sort block */
	void *sortinfo;			/* Optional sort block */
	uword *data_len;		/* Array of resource/record sizes */
	ubyte **data;			/* Array of resources/records */
					/* XXX - This should also probably
					 * become a linked list, for the
					 * same reasons as above.
					 */
};

/* Convenience macros */
#define IS_RSRC_DB(db) 		((db)->header.attributes & PDB_ATTR_RESDB)

extern struct pdb *new_pdb();
extern void free_pdb(struct pdb *db);
extern struct pdb *pdb_Read(char *fname);
extern int pdb_Write(const struct pdb *db, const char *fname);
extern struct pdb *pdb_Download(
	struct PConnection *pconn,
	const struct dlp_dbinfo *dbinfo,
	ubyte dbh);
extern struct pdb_record *pdb_FindRecordByID(
	const struct pdb *db,
	const udword id);
extern struct pdb_record *pdb_FindRecordByIndex(
	const struct pdb *db,
	const uword index);
extern int UploadDatabase(struct PConnection *pconn, const struct pdb *db);

/* XXX - Functions to write:
pdb_Upload		upload to Palm
pdb_Download		download from Palm
pdb_setAppInfo		set the appinfo block
pdb_setSortInfo		set the sortinfo block
pdb_getRecordByID	return a pointer to a pdb_record, given its ID
pdb_getResourceByID	return a pointer to a pdb_resource, given its ID
pdb_appendRecord	append a record to the list
pdb_appendResource	append a resource to the list
*/

#endif	/* _pdb_h_ */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
