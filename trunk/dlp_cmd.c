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

	DLP_TRACE(1, ">>> DlpEndOfSync status == %d\n", status);
	/* Fill in the header */
	header.id = DLPREQ_ENDOFSYNC;
	header.argc = 1;

	/* Fill in the argument */
	args[0].id = 0x20;
	args[0].size = 2;		/* Sending a uword */
	args[0].data = &status;		/* XXX - Endianness will break for non-zero values */

	err = dlp_send_req(fd,
			   &header,
			   args);

	return err;
}

int
DlpAddSyncLogEntry(int fd,	/* File descriptor to write to */
		    char *msg)	/* Log message */
{
	int err;
	struct dlp_req_header header;
	struct dlp_arg args[1];

	DLP_TRACE(1, ">>> DlpAddSyncLogEntry msg == \"%s\"\n", msg);
	/* Fill in the header */
	header.id = DLPREQ_ADDSYNCLOGENTRY;
	header.argc = 1;

	/* Fill in the argument */
	args[0].id = 0x20;
	args[0].size = strlen(msg)+1;
	args[0].data = msg;

	err = dlp_send_req(fd, &header, args);
	return err;
}
