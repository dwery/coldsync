/* coldsync.c
 *
 * $Id: coldsync.c,v 1.2 1999-02-21 08:58:27 arensb Exp $
 */
#include <stdio.h>
#include <pconn/palm_errno.h>
#include <pconn/PConnection.h>
#include <pconn/cmp.h>
#include <pconn/dlp_cmd.h>
#include "coldsync.h"

int Cold_Connect(int fd, const char *name);
int Cold_Disconnect(int fd, const ubyte status);
int Cold_GetMemInfo(int fd, struct ColdPalm *palm);
int Cold_ListDBs(int fd, struct ColdPalm *palm);

struct ColdPalm palm;

int
main(int argc, char *argv[])
{
	int fd;
	int err;

	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s device\n", argv[0]);
		exit(1);
	}

	/* XXX - In the production version (daemon), this should just
	 * set up the serial port normally (raw, 9600 bps), then wait
	 * for it to become readable. Then fork, and let the child
	 * establish the connection and sync.
	 */
	if ((fd = new_PConnection(argv[1])) == 0)
	{
		fprintf(stderr, "Error: can't open connection.\n");
		exit(1);
	}

	/* Connect to the Palm */
	if ((err = Cold_Connect(fd, argv[1])) < 0)
	{
		fprintf(stderr, "Can't connect to Palm\n");
		exit(1);
	}

Cold_GetMemInfo(fd, &palm);

	palm.num_cards = 1;	/* XXX - Should find this out */

	/* Get a list of all databases on the Palm */
	Cold_ListDBs(fd, &palm);

	/* Finally, close the connection */
	if ((err = Cold_Disconnect(fd, DLPCMD_SYNCEND_NORMAL)) < 0)
	{
		fprintf(stderr, "Error disconnecting\n");
		exit(1);
	}

	exit(0);
}

/* Cold_Connect
 * Wait for a Palm to show up on the other end.
 */
int
Cold_Connect(int fd,
	  const char *name)
{
	int err;
	struct slp_addr pcaddr;
	struct cmp_packet cmpp;

	pcaddr.protocol = SLP_PKTTYPE_PAD;	/* XXX - This ought to
						 * be part of the
						 * initial socket
						 * setup.
						 */
	pcaddr.port = SLP_PORT_DLP;
	PConn_bind(fd, &pcaddr);
	/* XXX - PConn_accept(fd) */

	printf("===== Waiting for wakeup packet\n");
	do {
		err = cmp_read(fd, &cmpp);
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
	cmpp.ver_major = 1;	/* XXX - Should be constants in header file */
	cmpp.ver_minor = 1;
	cmpp.rate = 0;	/* Should be able to set a different rate */
	printf("===== Sending INIT packet\n");
	cmp_write(fd, &cmpp);
	printf("===== Finished sending INIT packet\n");

	return 0;
}

int
Cold_Disconnect(int fd, const ubyte status)
{
	int err;

	/* Terminate the sync */
	err = DlpEndOfSync(fd, status/*DLPCMD_SYNCEND_NORMAL*/);
	if (err < 0)
	{
		fprintf(stderr, "Error during DlpEndOfSync: (%d) %s\n",
			palm_errno,
			palm_errlist[palm_errno]);
		return err;
	}
	fprintf(stderr, "===== Finished syncing\n");

	PConnClose(fd);		/* Close the connection */

	return 0;
}

int
Cold_GetMemInfo(int fd, struct ColdPalm *palm)
{
	int err;
	ubyte last_card;
	ubyte more;
	struct dlp_cardinfo cinfo;
extern int dlpc_debug;

dlpc_debug = 10;

	err = DlpReadStorageInfo(fd, 0, &last_card, &more,
				 &cinfo);
	return err;
}

/* XXX - This ought to find out the memory info first (including the
 * number of databases, then get them all and put them in a list in
 * 'palm'.
 */
int
Cold_ListDBs(int fd, struct ColdPalm *palm)
{
	int err;
	int card;

	/* Iterate over each memory card */
	for (card = 0; card < /*palm->num_cards*/2; card++)
	{
		ubyte iflags;
		uword start;
		uword last_index;
		ubyte oflags;
		ubyte num;
		struct dlp_dbinfo dbinfo;

		/* Just read the databases in RAM (for now(?)) */
		iflags = DLPCMD_READDBLFLAG_RAM/* | DLPCMD_READDBLFLAG_ROM*/;

		err = 0;
		start = 0;
		do {
fprintf(stderr, "===== Reading DB list for card %d, start %d\n", card, start);
			err = DlpReadDBList(fd, iflags, card, start,
					    &last_index, &oflags, &num,
					    &dbinfo);
printf("=== Database: \"%s\"\n", dbinfo.name);
			start = last_index+1; 
		} while (err == 0);
	}

	return 0;
}
