#include <stdio.h>
#include <unistd.h>		/* For sleep() */
#include <time.h>
#include "PConnection.h"
#include "slp.h"
#include "padp.h"
#include "cmp.h"
#include "dlp_cmd.h"
#include "palm_errno.h"
#include "util.h"

extern int slp_debug;
extern int padp_debug;
extern int cmp_debug;
extern int dlp_debug;
extern int dlpc_debug;

int
main(int argc, char *argv[])
{
/*  	int fd; */
	struct PConnection *pconn;
	int i;
	int err;
	struct slp_addr pcaddr;
	ubyte buf[1024];
/*  	const ubyte *ptr; */
	uword len;
	struct cmp_packet cmpp;
	struct dlp_netsyncinfo netsyncinfo;
	struct dlp_writenetsyncinfo writenetsyncinfo;
	struct dlp_sysinfo sysinfo;
	struct dlp_userinfo userinfo;
	struct dlp_setuserinfo moduserinfo;
	struct dlp_time ptime;
	ubyte dbh;		/* Database handle */
	struct dlp_appblock appblock;
	struct dlp_sortblock sortblock;
	struct dlp_dbinfo dbinfo;
	struct dlp_opendbinfo opendbinfo;
	struct dlp_idlistreq idreq;
	uword numrecs;
	udword recid;
	udword recids[128];
	struct dlp_createdbreq newdbreq;
	struct dlp_readrecreq_byid readbyid;
	struct dlp_readrecreq_byindex readbyindex;
	struct dlp_readrecret readret;
	struct dlp_writerec writerec;
	udword featvalue;
	struct dlp_resource resource;
	struct dlp_apppref pref;
	struct dlp_appcall appcall;
	struct dlp_appresult appresult;
	ubyte last_card;
	ubyte more;
	struct dlp_cardinfo cardinfo;
	const ubyte *rptr;	/* Pointer into buffers (for reading) */

	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s device\n", argv[0]);
		exit(1);
	}

	if ((pconn = new_PConnection(argv[1])) == 0)
	{
		fprintf(stderr, "Error: can't open connection.\n");
		exit(1);
	}

	pcaddr.protocol = SLP_PKTTYPE_PAD;	/* XXX - This ought to
						 * be part of the
						 * initial socket
						 * setup.
						 */
	pcaddr.port = SLP_PORT_DLP;
	PConn_bind(pconn, &pcaddr);
	/* XXX - PConn_accept(fd) */

slp_debug = 100;
padp_debug = 6/*100*/;
cmp_debug = 100;
dlp_debug = 100;
dlpc_debug = 100;
	printf("===== Waiting for wakeup packet\n");
	do {
		err = cmp_read(pconn, &cmpp);
		if (err < 0)
		{
			if (palm_errno == PALMERR_TIMEOUT)
				continue;
			fprintf(stderr, "Error during cmp_read: (%d) %s\n",
				palm_errno,
				palm_errlist[palm_errno]);
			exit(1);
		}
	} while (cmpp.type != CMP_TYPE_WAKEUP);
	printf("===== Got a wakeup packet\n");
	/* Compose a reply */
	cmpp.type = CMP_TYPE_INIT;
	cmpp.ver_major = 1;
	cmpp.ver_minor = 1;
	cmpp.rate = 0;
	printf("===== Sending INIT packet\n");
	cmp_write(pconn, &cmpp);
	printf("===== Finished sending INIT packet\n");

	/* Read a feature (the version of the net library, in this case) */
	err = DlpReadFeature(pconn, MAKE_CHUNKID('n','e','t','l'), 0, &featvalue);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpReadFeature: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		exit(1);
	}
	printf("===== Read a feature\n");

	/* Get net sync info */
	err = DlpReadNetSyncInfo(pconn, &netsyncinfo);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpReadNetSyncInfo: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		exit(1);
	}
	printf("===== Got net sync info\n");

	/* Set net sync info */
	writenetsyncinfo.modflags =
0xff;
/*  		DLPCMD_MODNSFLAG_LANSYNC | */
/*  		DLPCMD_MODNSFLAG_HOSTNAME | */
/*  		DLPCMD_MODNSFLAG_HOSTADDR | */
/*  		DLPCMD_MODNSFLAG_NETMASK; */
	writenetsyncinfo.netsyncinfo.lansync_on = 1;
	writenetsyncinfo.netsyncinfo.hostnamesize =
		strlen("kickass.ooblick.com")+1;
	writenetsyncinfo.netsyncinfo.hostaddrsize =
		strlen("192.168.132.61")+1;
	writenetsyncinfo.netsyncinfo.hostnetmasksize =
		strlen("255.255.255.0")+1;
	memcpy(writenetsyncinfo.netsyncinfo.synchostname,
	       "kickass.ooblick.com",
	       strlen("kickass.ooblick.com")+1);
	memcpy(writenetsyncinfo.netsyncinfo.synchostaddr,
	       "192.168.132.61",
	       strlen("192.168.132.61")+1);
	memcpy(writenetsyncinfo.netsyncinfo.synchostnetmask,
	       "255.255.255.0",
	       strlen("255.255.255.0")+1);
	err = DlpWriteNetSyncInfo(pconn, &writenetsyncinfo);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpWriteNetSyncInfo: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		exit(1);
	}
	printf("===== Wrote net sync info\n");

	/* Get system info */
	err = DlpReadSysInfo(pconn, &sysinfo);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpReadSysInfo: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		exit(1);
	}
	printf("===== Got system info\n");

	/* Get user info */
	err = DlpReadUserInfo(pconn, &userinfo);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpReadUserInfo: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		exit(1);
	}
	printf("===== Got user info\n");

#if 0
	/* Set user info */
	moduserinfo.userid = 2072;
	moduserinfo.lastsyncPC = 0xc0a8843d;	/* 192.168.132.61 */
	moduserinfo.username = "Arnie Arnesson";
	moduserinfo.usernamelen = strlen(moduserinfo.username) + 1;
	moduserinfo.modflags = DLPCMD_MODUIFLAG_USERID |
		DLPCMD_MODUIFLAG_USERNAME;
	DlpWriteUserInfo(pconn, &moduserinfo);
	printf("===== Set user info\n");
#endif	/* 0 */

	/* Get the time */
	err = DlpGetSysDateTime(pconn, &ptime);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpGetSysDateTime: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		exit(1);
	}
	printf("===== Got the time\n");

	/* Set the time */
	ptime.minute += 5;
	ptime.minute %= 60;
/*  	DlpSetSysDateTime(pconn, &ptime); */
	printf("===== Set the time\n");

	err = DlpReadStorageInfo(pconn, 0, &last_card, &more, &cardinfo);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpReadStorageInfo: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		exit(1);
	}
	printf("===== Got storage info\n");

#if 1
/*  slp_debug = 0; */
/*  padp_debug = 0; */
/*  dlp_debug = 0; */
	i = 0;
	do {
		uword last_index;
		ubyte oflags;
		ubyte num;

		err = DlpReadDBList(pconn,
				    DLPCMD_READDBLFLAG_RAM |
				    DLPCMD_READDBLFLAG_ROM,
				    0, i,
				    &last_index, &oflags, &num,
				    &dbinfo);
		if (err < 0)
		{
			fprintf(stderr, "Error during DlpReadDBList: (%d) %s\n",
				palm_errno,
				palm_errlist[palm_errno]);
			exit(1);
		}
		i++;
	} while (err > 0);
#endif	/* 0 */

	/* Open a conduit */
	err = DlpOpenConduit(pconn);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpOpenConduit: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		exit(1);
	}
	fprintf(stderr, "===== Opened a conduit\n");

	/* Open a database */
	err = DlpOpenDB(pconn, 0, "AddressDB",
			DLPCMD_MODE_READ |
			DLPCMD_MODE_WRITE |
			DLPCMD_MODE_SECRET,
			&dbh);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpOpenDB: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		exit(1);
	}
	fprintf(stderr, "===== Opened a database\n");

	/* Read an app block */
	appblock.dbid = dbh;
	appblock.offset = 0;
	appblock.len = 0xffff;		/* Read to the end */
	/* XXX - Need a constant for "read to the end" */
	err = DlpReadAppBlock(pconn, dbh, 0, 0xffff, &len, &rptr);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpReadAppBlock: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		exit(1);
	}
	fprintf(stderr, "===== Read an app block\n");

#if 0
	appblock.dbid = dbh;
	appblock.len = len;
	err = DlpWriteAppBlock(pconn, &appblock, buf);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpWriteAppBlock: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		exit(1);
	}
	fprintf(stderr, "===== Wrote an app block\n");
#endif	/* 0 */

	/* Read a sort block */
	sortblock.dbid = dbh;
	sortblock.offset = 0;
	sortblock.len = 0xffff;		/* Read to the end */
	err = DlpReadSortBlock(pconn, &sortblock, &len, &rptr);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpReadSortBlock: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		exit(1);
	}
	fprintf(stderr, "===== Read a sort block\n");

#if 0
	sortblock.dbid = dbh;
	sortblock.len = len;
	err = DlpWriteSortBlock(pconn, &sortblock, buf);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpWriteSortBlock: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		exit(1);
	}
	fprintf(stderr, "===== Wrote a sort block\n");
#endif	/* 0 */

	/* Get the number of records in the database */
	err = DlpReadOpenDBInfo(pconn, dbh, &opendbinfo);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpReadOpenDBInfo: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		exit(1);
	}
	fprintf(stderr, "===== Got open DB info\n");
	fprintf(stderr, "There are %d records\n", opendbinfo.numrecs);

	/* Get the record IDs */
	/* XXX - Need const for "to the end" */
	err = DlpReadRecordIDList(pconn, dbh, 0x00, 0, 0xffff,
				  &numrecs, recids);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpReadRecordIDList: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		/*  exit(1); */
	}
	fprintf(stderr, "===== Read record ID list\n");
	fprintf(stderr, "There are %d IDs\n", numrecs);
	for (i = 0; i < numrecs; i++)
	{
		fprintf(stderr, "\tID %d: %ld (0x%08lx)\n",
			i, recids[i], recids[i]);
	}

	/* Read records by ID */
slp_debug = 100;
padp_debug = 100;
dlp_debug = 100;
dlpc_debug = 100;
	fprintf(stderr, "+++++ Reading database by record IDs\n");
	for (i = 0; i < numrecs; i++)
	{
		fprintf(stderr, "Reading record ID %ld\n", recids[i]);
		readbyid.dbid = dbh;
		readbyid.recid = recids[i];
		readbyid.offset = 0;
		readbyid.len = 0xffff;	/* XXX - Need const */
		err = DlpReadRecordByID(pconn, &readbyid, &readret);
		if (err < 0)
		{
			fprintf(stderr, "Error during DlpReadRecordByID: (%d) %s\n",
				palm_errno,
				palm_errlist[palm_errno]);
			exit(1);
		}
memcpy(buf, readret.data, readret.size);
	}

#if 0
	fprintf(stderr, "+++++ Reading database by record indexes\n");
	for (i = 0; i < numrecs; i++)
	{
		fprintf(stderr, "Reading index %d\n", i);
		readbyindex.dbid = dbh;
		readbyindex.index = i;
		readbyindex.offset = 0;
		readbyindex.len = 0xffff;	/* XXX - Need const */
		err = DlpReadRecordByIndex(pconn, &readbyindex, &readret);
		if (err < 0)
		{
			fprintf(stderr, "Error during DlpReadRecordByIndex: (%d) %s\n",
				palm_errno,
				palm_errlist[palm_errno]);
			exit(1);
		}
	}
#endif	/* 0 */

#if 1
	fprintf(stderr, "+++++ Reading next modified recs\n");
	for (i = 0; i < numrecs+1; i++)
	{
		fprintf(stderr, "Reading %d\n", i);
		err = DlpReadNextModifiedRec(pconn, dbh, &readret);
		if (err < 0)
		{
			fprintf(stderr, "Error during DlpReadNextModifiedRec: (%d) %s\n",
				palm_errno,
				palm_errlist[palm_errno]);
			/*  exit(1); */
		}
	}

	/* Reset the "next modified record" index and try again */
	err = DlpResetRecordIndex(pconn, dbh);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpResetRecordIndex: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		exit(1);
	}

	fprintf(stderr, "+++++ Reading next recs in category\n");
	for (i = 0; i < numrecs+1; i++)
	{
		fprintf(stderr, "Reading %d\n", i);
		err = DlpReadNextRecInCategory(pconn, dbh, 1, &readret);
		if (err < 0)
		{
			fprintf(stderr, "Error during DlpReadNextRecInCategory: (%d) %s\n",
				palm_errno,
				palm_errlist[palm_errno]);
			/*  exit(1); */
		}
	}

	fprintf(stderr, "+++++ Reading next modified recs in category\n");
	for (i = 0; i < numrecs+1; i++)
	{
		fprintf(stderr, "Reading %d\n", i);
		err = DlpReadNextModifiedRecInCategory(pconn, dbh, 1, &readret);
		if (err < 0)
		{
			fprintf(stderr, "Error during DlpReadNextModifiedRecInCategory: (%d) %s\n",
				palm_errno,
				palm_errlist[palm_errno]);
			/*  exit(1); */
		}
	}
#endif	/* 0 */

#if 0
	/* Delete a record */
	err = DlpDeleteRecord(pconn, dbh, /*0*/0x80, recids[0]);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpDeleteRecord: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		exit(1);
	}
	fprintf(stderr, "===== Deleted a record\n");
#endif

	/* Move a category */
	err = DlpMoveCategory(pconn, dbh, 2, 0);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpMoveCategory: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		exit(1);
	}
	fprintf(stderr, "===== Moved a category\n");

	/* Clean up the database: delete everything that was marked
	 * deleted.
	 */
	err = DlpCleanUpDatabase(pconn, dbh);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpCleanUpDatabase: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		exit(1);
	}
	fprintf(stderr, "===== Cleaned up the database\n");

	/* Reset the sync flags */
	err = DlpResetSyncFlags(pconn, dbh);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpResetSyncFlags: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		exit(1);
	}
	fprintf(stderr, "===== Reset sync flags\n");

	/* Close the database */
	err = DlpCloseDB(pconn, /*dbh*/DLPCMD_CLOSEALLDBS);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpCloseDB: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		exit(1);
	}
	fprintf(stderr, "===== Closed the database\n");

	/* Open the "Saved Preferences" database */
	err = DlpOpenDB(pconn, 0, "Saved Preferences",
			DLPCMD_MODE_READ |
			DLPCMD_MODE_WRITE |
			DLPCMD_MODE_SECRET,
			&dbh);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpOpenDB: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		exit(1);
	}
	fprintf(stderr, "===== Opened Saved Preferences database\n");

	/* Read some resources */
	for (i = 0; ; i++)
	{
		err = DlpReadResourceByIndex(pconn, dbh, i, 0,
					     0xffff,	/* XXX - Need const */
					     &resource,
					     buf);
		if (err < 0)
		{
			fprintf(stderr, "Error during DlpReadResourceByIndex: (%d) %s\n",
				palm_errno,
				palm_errlist[palm_errno]);
			/*  exit(1); */
			break;
		}
		fprintf(stderr, "Resource %d:\n", i);
		fprintf(stderr, "\ttype %c%c%c%c (0x%08lx)\n",
			(char) (resource.type >> 24) & 0xff,
			(char) (resource.type >> 16) & 0xff,
			(char) (resource.type >> 8) & 0xff,
			(char) resource.type & 0xff,
			resource.type);
		fprintf(stderr, "\tid %d\n", resource.id);
		fprintf(stderr, "\tindex %d\n", resource.index);
		fprintf(stderr, "\tsize %d\n", resource.size);
		fprintf(stderr, "\tdata:\n");
		debug_dump(stderr, "\t| ", buf, resource.size);
	}

	/* Read a resource by type */
	err = DlpReadResourceByType(pconn, dbh, MAKE_CHUNKID('o','w','n','r'),
				    1, 0, 0xffff,
				    &resource, buf);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpReadResourceByIndex: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		/*  exit(1); */
	}
	fprintf(stderr, "Resource %d:\n", i);
	fprintf(stderr, "\ttype %c%c%c%c (0x%08lx)\n",
		(char) (resource.type >> 24) & 0xff,
		(char) (resource.type >> 16) & 0xff,
		(char) (resource.type >> 8) & 0xff,
		(char) resource.type & 0xff,
		resource.type);
	fprintf(stderr, "\tid %d\n", resource.id);
	fprintf(stderr, "\tindex %d\n", resource.index);
	fprintf(stderr, "\tsize %d\n", resource.size);
	fprintf(stderr, "\tdata:\n");
	debug_dump(stderr, "\t| ", buf, resource.size);

#if 1
	err = DlpDeleteResource(pconn, dbh, 0x00,	/* XXX - Need flag consts */
				MAKE_CHUNKID('o','w','n','r'), 1);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpDeleteResource: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		exit(1);
	}
	fprintf(stderr, "===== Deleted a resource\n");
#endif	/* 0 */

#if 1
	/* Write a resource */
	buf[0] = '\0';
	buf[1] = '\1';
	sprintf((char *) buf+2, "Ce pilote appartient a:\nHello, world! (%ld)\n",
		time(NULL));
	err = DlpWriteResource(pconn, dbh, MAKE_CHUNKID('o','w','n','r'),
			       1, strlen((char *) buf+2)+3, buf);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpWriteResource: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		exit(1);
	}
	fprintf(stderr, "===== Wrote a resource\n");
#endif	/* 0 */

	/* Close the database */
	err = DlpCloseDB(pconn, /*dbh*/DLPCMD_CLOSEALLDBS);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpCloseDB: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		exit(1);
	}
	fprintf(stderr, "===== Closed the database\n");

/*  DlpOpenConduit(fd); */
	/* Create a new database */
#if 0
slp_debug = 100;
padp_debug = 100;
dlp_debug = 100;
	newdbreq.creator = 'Cold';
	newdbreq.type = 'FooB';
	newdbreq.card = 0;	
	newdbreq.flags = DLPCMD_DBFLAG_BACKUP;
	newdbreq.version = 1;
	strcpy(newdbreq.name, "Foobar!");
	err = DlpCreateDB(pconn, &newdbreq, &dbh);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpCreateDB: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		/*  exit(1); */
	}
	fprintf(stderr, "===== Created a database\n");

#if 0
	/* Add some records to it. */
	/* XXX - I don't know the format for a record */
	writerec.dbid = dbh;
	writerec.flags = 0x80;	/* XXX - Need constant */
	writerec.recid = 0;
	writerec.attributes = 0;
	writerec.category = 0;
	writerec.data = buf/*"foo"*/;
	err = DlpWriteRecord(pconn, readret.size/*4*/, &writerec, &recid);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpWriteRecord: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		exit(1);
	}
	fprintf(stderr, "===== Wrote a record\n");
#endif	/* 0 */

	/* Close the database */
	err = DlpCloseDB(pconn, /*dbh*/DLPCMD_CLOSEALLDBS);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpCloseDB: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		exit(1);
	}
	fprintf(stderr, "===== Closed the database\n");

	/* Delete the database */
	err = DlpDeleteDB(pconn, 0, "Foobar!");
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpDeleteDB: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		exit(1);
	}
	fprintf(stderr, "===== Deleted the database\n");
#endif	/* 0 */

#if 0
	/* Read a preference */
	/* XXX - Need to come up with some values to feed it */
	err = DlpReadAppPreference(pconn, 
				   /* 'psys', 1 works. What else is there? */
				   'psys', 1,
				   0xffff, 0, &pref, buf);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpReadAppPreference: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		/*  exit(1); */
	}
	fprintf(stderr, "===== Read a preference\n");
	fprintf(stderr, "Preference: version %d, size %d, len %d\n",
		pref.version, pref.size, pref.len);
	debug_dump(stderr, "\t| ", buf, pref.len);
#endif	/* 0 */

#if 0
	/* Call an application */
	appcall.creator = 'addr';
	appcall.type = 'appl';
	appcall.action = 18;		/* XXX - Need const */	
	err = DlpCallApplication(pconn, sysinfo.rom_version, &appcall,
				 0, NULL,
				 &appresult);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpCallApplication: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		exit(1);
	}
	fprintf(stderr, "===== Called an application\n");
#endif	/* 0 */

	/* Write the log */
	err = DlpAddSyncLogEntry(pconn, "Stuff looks hunky-dory!");
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpAddSyncLogEntry: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		exit(1);
	}
	fprintf(stderr, "===== Wrote the log\n");

#if 0
	/* Reset the system */
	err = DlpResetSystem(fd);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpResetSystem: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		exit(1);
	}
	fprintf(stderr, "===== Reset the system\n");
#endif	/* 0 */

	/* Terminate the sync */
	err = DlpEndOfSync(pconn, DLPCMD_SYNCEND_NORMAL);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpEndOfSync: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		exit(1);
	}
	fprintf(stderr, "===== Finished syncing\n");

	PConnClose(pconn);		/* Close the connection */

	exit(0);
}
