/* coldsync.c
 *
 * $Id: coldsync.c,v 1.1 1999-02-19 23:00:14 arensb Exp $
 */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include "palm/pdb.h"

int
main(int argc, char *argv[])
{
	int fd;
	int i;
	struct pdb *db;

	if (argc < 2)
	{
		fprintf(stderr, "Usage: %s filename...\n", argv[0]);
		exit(1);
	}

	/* Parse each database in turn */
	for (i = 1; i < argc; i++)
	{
		printf("Reading \"%s\"\n", argv[i]);
		if ((fd = open(argv[i], O_RDONLY))
		    < 0)
		{
			perror("open");
			exit(1);
		}

		if ((db = read_pdb(fd)) == NULL)
		{
			fprintf(stderr, "can't read db \"%s\"\n", argv[i]);
/*  			exit(1); */
		}

		close(fd);
	}

	exit(0);
}
