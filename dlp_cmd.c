#include <stdio.h>
#include "padp.h"
#include "dlp.h"

int
DlpEndOfSync(int fd,
	     uword status)	/* Exit status */
{
	int err;
	struct dlp_req_header header;
	struct dlp_arg args[1];
	static ubyte ostatus[2];	/* Buffer for status */
	struct dlp_resp_header ret_head;
	struct dlp_arg *ret_argv;

	DLP_TRACE(1, ">>> DlpEndOfSync status == %d\n", status);
	/* Fill in the header */
	header.id = DLPREQ_ENDOFSYNC;
	header.argc = 1;

	/* Fill in the argument */
	args[0].id = 0x20;
	args[0].size = 2;		/* Sending a uword */
	/* Convert status to big-endian order */
	ostatus[0] = (status >> 8) & 0xff;
	ostatus[1] = status & 0xff;
	args[0].data = (ubyte *) &ostatus;

	/* XXX - Error-checking */
	err = dlp_send_req(fd,
			   &header,
			   args);
	/* XXX - Error-checking */
/*  	err = dlp_read_resp(fd); */
	err = dlp_read_resp(fd, &ret_head, &ret_argv);

	dlp_free_arglist(ret_head.argc, ret_argv);

	return err;
}

int
DlpAddSyncLogEntry(int fd,	/* File descriptor to write to */
		   char *msg)	/* Log message */
{
	int err;
	struct dlp_req_header header;
	struct dlp_arg args[1];
	struct dlp_resp_header ret_head;
	struct dlp_arg *ret_argv;

	DLP_TRACE(1, ">>> DlpAddSyncLogEntry msg == \"%s\"\n", msg);
	/* Fill in the header */
	header.id = DLPREQ_ADDSYNCLOGENTRY;
	header.argc = 1;

	/* Fill in the argument */
	args[0].id = 0x20;
	args[0].size = strlen(msg)+1;
	args[0].data = msg;

	/* XXX - Error-checking */
	err = dlp_send_req(fd, &header, args);
	/* XXX - Error-checking */
/*  	err = dlp_read_resp(fd); */
	err = dlp_read_resp(fd, &ret_head, &ret_argv);

	dlp_free_arglist(ret_head.argc, ret_argv);

	return err;
}

int
DlpReadSysInfo(int fd,		/* File descriptor to read from */
	       struct dlp_sysinfo *sysinfo)
				/* This will be filled in with system
				 * information about the Palm. */
{
	int i;
	int err;
	struct dlp_req_header header;
	struct dlp_resp_header ret_head;
	struct dlp_arg *ret_argv;

	DLP_TRACE(1, ">>> DlpReadSysInfo\n");

	/* Fill in the header */
	header.id = DLPREQ_READSYSINFO;
	header.argc = 0;

	/* XXX - Error-checking */
	err = dlp_send_req(fd, &header, NULL);
	/* XXX - Error-checking */
/*  	err = dlp_read_resp(fd); */
	err = dlp_read_resp(fd, &ret_head, &ret_argv);
fprintf(stderr, "--- back in DlpReadSysInfo\n");

	if (ret_head.argc != 1)
		fprintf(stderr, "DlpReadSysInfo: expected 1 argument, got %d\n",
			ret_head.argc);
fprintf(stderr, "Extracting sysinfo information\n");
	sysinfo->rom_version =
		((udword) ret_argv[0].data[0] << 24) |
		((udword) ret_argv[0].data[1] << 16) |
		((udword) ret_argv[0].data[2] <<  8) |
		ret_argv[0].data[3];
	sysinfo->localization =
		((udword) ret_argv[0].data[4] << 24) |
		((udword) ret_argv[0].data[5] << 16) |
		((udword) ret_argv[0].data[6] <<  8) |
		ret_argv[0].data[7];
	sysinfo->prodIDsize = ret_argv[0].data[8];
	for (i = 0; i < sysinfo->prodIDsize; i++)
	{
		fprintf(stderr, "%02x ", ret_argv[0].data[9+i]);
		if ((i & 0x0f) == 0x0f)
			fprintf(stderr, "\n");
	}
	if ((i & 0x0f) != 0)
		fprintf(stderr, "\n");
fprintf(stderr, "sysinfo: ROM version 0x%08x, loc 0x%08x, prodIDsize %d, prodID %x\n",
	sysinfo->rom_version,
	sysinfo->localization,
	sysinfo->prodIDsize,
	sysinfo->prodID);

	dlp_free_arglist(ret_head.argc, ret_argv);

	return err;
}
