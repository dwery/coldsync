/*
	Very simple example of a ColdSync sync conduit in C.

	It demonstrates an interesting function: to be able to use the
	DLP (PalmOS RPC) function set from a C conduit.

	It's also not one of my better pieces of code. You're better off throwing
	away the code and using the concepts than trying to expand this into a
	"real" conduit.

	To build (from coldsync/conduits):
	
		gcc -g -o time-conduit -I.. -I../include \
			-L../libpconn -L../libpdb \
			-lusb -lcap time-conduit.c -lpconn


	christophe.beauregard@sympatico.ca
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>

#include <pconn/spc_client.h>
#include <pconn/dlp_cmd.h>

int main( int ac, char* av[] ) {
	PConnection* pconn = NULL;
	struct dlp_time dt;
	time_t t;
	struct tm* now;
	int spc_fd = -1;
	char* tmp;
	char buf[PATH_MAX];
	char* prefs[256];	/* arbitrary, but large enough */
	unsigned int nprefs = 0;
	char* headers[256];
	unsigned int nheaders = 0;

	memset( &dt, 0, sizeof(dt) );
	memset( prefs, 0, sizeof(prefs) );
	memset( headers, 0, sizeof(headers) );

	/* not pretty, but all we expect is "-config" or "conduit sync" */
	if( ac == 2 ) {
		if( strcmp(av[1],"-config") ) {
			fprintf( stderr, "usage: %s <flavor|-config>\n", av[0] );
			exit( -1 );
		}

		printf(
			"conduit sync {\n"
			"\tpath: \"%s\"\n"
			"\ttype: none\n"
			"}\n", av[0] );

		exit( 0 );
	} else if( ac == 3 && !strcmp(av[1],"conduit") ) {
		if( strcmp(av[2],"sync") ) {
			fprintf( stderr, "401 Unknown flavor '%s'\n", av[2] );
			exit( -1 );
		}
	} else {
		fprintf( stderr, "usage: %s -config\n", av[0] );
		exit( -1 );
	}

	/* read headers */
	while( fgets(buf,sizeof(buf),stdin) != NULL ) {
		buf[sizeof(buf)-1] = 0;	/* fgets() is pretty stupid */
		tmp = strrchr( buf, '\n' );	/* fgets doesn't strip trailing newline */
		if( tmp ) *tmp = 0;

		if( buf[0] == 0 ) break;	/* end of headers */

		tmp = strchr(buf,':');
		if( tmp == NULL ) {
			printf( "401 Invalid input [%s]\n", buf );
			exit( -1 );
		}
		tmp ++;
		while( *tmp && isspace(*tmp) ) tmp++;

		if( !strncmp("Preference:", buf, 11 ) ) {
			prefs[nprefs++] = strdup( tmp );
		} else {
			headers[nheaders++] = strdup( tmp );
			
			/* For this conduit, we only care about SPCpipe. The rest of the
			headers are gravy */
			if( !strncmp( "SPCPipe:", buf, 8 ) ) {
				spc_fd = atoi( tmp );
			}
		}
	}

	if( spc_fd < 0 ) {
		printf( "401 Unable to get SPC pipe\n" );
		exit( -1 );
	}

	/* open a SPC connection */
	pconn = new_spc_client( spc_fd );
	if( pconn == NULL ) {
		printf( "401 Failed to create SPC connection\n" );
		exit( -1 );
	}

	/* feed time to PDA */
	t = time(NULL);
	now = localtime(&t);
	if( now == NULL ) {
		printf( "401 %s\n", strerror(errno) );
		exit( -1 );
	}

	dt.year = now->tm_year + 1900;
	dt.month = now->tm_mon + 1;
	dt.day = now->tm_mday;
	dt.hour = now->tm_hour;
	dt.minute = now->tm_min;
	dt.second = now->tm_sec;

	DlpSetSysDateTime( pconn, &dt );

	printf( "201 Timing is everything\n" );
	exit( 0 );
}
