/* pdb.h
 *
 * Definitions and such for Palm databases.
 *
 * $Id: pdb.h,v 1.1 1999-02-19 22:44:42 arensb Exp $
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
	ubyte uniqueID[3];		/* Record's unique ID. Legal records
					 * do not have an ID of 0. */
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
	void *appinfo;			/* Optional app info block */
	void *sortinfo;			/* Optional sort info block */
	struct pdb_record *records;	/* Array of records */
					/* XXX - This should also probably
					 * become a linked list, for the
					 * same reasons as above.
					 */
};

extern struct pdb *read_pdb(int fd);
extern void free_pdb(struct pdb *db);

#endif	/* _pdb_h_ */

/* This is for Emacs's benefit:
 * Local Variables: ***
 * fill-column:	75 ***
 * End: ***
 */
