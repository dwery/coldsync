#include <stdio.h>
#include <unistd.h>		/* For sleep() */
#include "PConnection.h"
#include "slp.h"
#include "padp.h"
#include "cmp.h"
#include "dlp_cmd.h"

extern int slp_debug;
extern int padp_debug;
extern int cmp_debug;
extern int dlp_debug;
extern int dlpcmd_debug;

int
main(int argc, char *argv[])
{
	int fd;
	int i;
	int err;
	struct slp_addr pcaddr;
/*  	ubyte inbuf[1024]; */
	struct cmp_packet cmpp;
	struct dlp_sysinfo sysinfo;
	struct dlp_userinfo userinfo;
	struct dlp_setuserinfo moduserinfo;
	struct dlp_time ptime;

	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s device\n", argv[0]);
		exit(1);
	}

	if ((fd = new_PConnection(argv[1])) == 0)
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
	PConn_bind(fd, &pcaddr);

slp_debug = 100;
padp_debug = 100;
cmp_debug = 100;
dlp_debug = 100;
dlpcmd_debug = 100;
	printf("===== Waiting for wakeup packet\n");
	do {
		cmp_read(fd, &cmpp);
	} while (cmpp.type != CMP_TYPE_WAKEUP);
	printf("===== Got a wakeup packet\n");
	/* Compose a reply */
	cmpp.type = CMP_TYPE_INIT;
	cmpp.rate = 0;
	printf("===== Sending INIT packet\n");
	cmp_write(fd, &cmpp);
	printf("===== Finished sending INIT packet\n");

	/* Get system info */
	DlpReadSysInfo(fd, &sysinfo);
	printf("===== Got system info\n");

	/* Get user info */
	DlpReadUserInfo(fd, &userinfo);
	printf("===== Got user info\n");

	/* Set user info */
	moduserinfo.userid = 2072;
	moduserinfo.lastsyncPC = 0xc0a8843c;	/* 192.168.132.60 */
	moduserinfo.username = "Arnie Arnesson";
	moduserinfo.usernamelen = strlen(moduserinfo.username) + 1;
	moduserinfo.modflags = DLPCMD_MODUIFLAG_USERID |
		DLPCMD_MODUIFLAG_USERNAME;
/*  	DlpWriteUserInfo(fd, &moduserinfo); */
	printf("===== Set user info\n");

	/* Get the time */
	DlpGetSysDateTime(fd, &ptime);
	printf("===== Got the time\n");

	/* Set the time */
	ptime.minute += 5;
	ptime.minute %= 60;
/*  	DlpSetSysDateTime(fd, &ptime); */
	printf("===== Set the time\n");

	DlpReadStorageInfo(fd, 0);
/*  	DlpReadStorageInfo(fd, 1); */
/*  	DlpReadStorageInfo(fd, 2); */
/*  	DlpReadStorageInfo(fd, 3); */
/*  	DlpReadStorageInfo(fd, 4); */
	printf("===== Got storage info\n");

	i = 0;
	do {
		err = DlpReadDBList(fd, 0xc0 /* ROM + RAM */, 0, i);
		i++;
	} while (err > 0);

	/* Write the log */
	DlpAddSyncLogEntry(fd, "Stuff looks hunky-dory!");

	/* Terminate the sync */
	DlpEndOfSync(fd, 0);

	PConnClose(fd);		/* Close the connection */

	exit(0);
}
