/* pdb.h
 *
 * Definitions and such for Palm databases.
 *
 * $Id: pdb.h,v 1.5 1999-03-11 20:38:03 arensb Exp $
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
#define PDB_REC_DELETED		0x80	/* Delete this record next sync */
#define PDB_REC_DIRTY		0x40	/* Record has been modified */
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

#define PDB_HEADER_LEN		72	/* Length of header in a file */
#define PDB_RECORDLIST_LEN	6	/* Length of record index header in
					 * file */

/* pdb_record
 * A plain old record, containing arbitrary data.
 */
struct pdb_record
{
	struct pdb_record *next;	/* Next record on linked list */
	localID offset;			/* Offset of record in file */
	ubyte attributes;		/* Record attributes */
	udword uniqueID;		/* Record's unique ID. Actually,
					 * only the bottom 3 bytes are
					 * stored in the file, but for
					 * everything else, it's much
					 * easier to just consider this a
					 * 32-bit integer.
					 */
	uword data_len;			/* Length of this record */
	ubyte *data;			/* This record's data */
};
#define PDB_RECORDIX_LEN	8	/* Size of a pdb_record in a file */

/* pdb_resource
 * Mac hackers should feel at home here: the type of a resource is really a
 * 4-character category identifier, and the ID is an integer within that
 * category.
 */
struct pdb_resource
{
	struct pdb_resource *next;	/* Next resource on linked list */
	udword type;			/* Resource type */
	uword id;			/* Resource ID */
	localID offset;			/* Offset of resource in file */
	uword data_len;			/* Length of this resource */
	ubyte *data;			/* This resource's data */
};
#define PDB_RESOURCEIX_LEN	10	/* Size of a pdb_resource in a file */

/* pdb
 * Structure of a Palm database (file), both resource databases (.prc) and
 * record databases (.pdb).
 */
struct pdb
{
	long file_size;			/* Total length of file */

	char name[PDB_DBNAMELEN];	/* Database name */
	uword attributes;		/* Database attributes */
	uword version;			/* Database version */

	udword ctime;			/* Creation time */
	udword mtime;			/* Time of last modification */
	udword baktime;			/* Time of last backup */
	udword modnum;			/* Modification number */
			/* XXX - What exactly is the modification number?
			 * Does it get incremented each time you make any
			 * kind of change to the database?
			 */
	/* XXX - These two should probably be called something like
	 * appinfo_offset and sortinfo_offset, since "ID" is misleading.
	 */
	localID appinfo_offset;		/* Offset of AppInfo block in the
					 * file */
	localID sortinfo_offset;	/* Offset of sort block in the file */

	udword type;			/* Database type */
	udword creator;			/* Database creator */

	udword uniqueIDseed;		/* Used to generate unique IDs.
					 * Only the lower 3 bytes are used.
					 * The high byte is for alignment.
					 */

	localID next_reclistID;		/* ID of next record index in the
					 * file. In practice, this field is
					 * always zero.
					 */
	uword numrecs;			/* Number of records/resources in
					 * the file.
					 */

	long appinfo_len;		/* Length of AppInfo block */
	void *appinfo;			/* Optional AppInfo block */
	long sortinfo_len;		/* Length of sort block */
	void *sortinfo;			/* Optional sort block */

	/* Record/resource list. Each of these is actually a linked list,
	 * to make it easy to insert and delete records.
	 */
	/* XXX - A lot of code is effectively duplicated for records and
	 * resources. Would it be better to shift the 'union' down one
	 * level (i.e., make 'rec_index' a linked list of unions, rather
	 * than a union of linked lists), and coalesce a lot of this
	 * duplicated code?
	 */
	union {
		struct pdb_record *rec;
		struct pdb_resource *rsrc;
	} rec_index;
};

/* Convenience macros */
#define IS_RSRC_DB(db) 		((db)->attributes & PDB_ATTR_RESDB)
					/* Is this a resource database? If
					 * not, it must be a record
					 * database.
					 */

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
extern int pdb_DeleteRecordByID(
	struct pdb *db,
	const udword id);
extern int UploadDatabase(struct PConnection *pconn, const struct pdb *db);
extern int pdb_AppendRecord(struct pdb *db, struct pdb_record *newrec);
extern int pdb_AppendResource(struct pdb *db, struct pdb_resource *newrsrc);
extern int pdb_InsertRecord(struct pdb *db,
			    struct pdb_record *prev,
			    struct pdb_record *newrec);
extern int pdb_InsertResource(struct pdb *db,
			      struct pdb_resource *prev,
			      struct pdb_resource *newrsrc);
struct pdb_record *pdb_CopyRecord(const struct pdb *db,
				  const struct pdb_record *rec);
struct pdb_resource *pdb_CopyResource(const struct pdb *db,
				      const struct pdb_resource *rsrc);

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
