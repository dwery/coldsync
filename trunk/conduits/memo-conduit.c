/*
	More complicated example of a ColdSync sync conduit in C.

	This is ugly, but it shows you how you can read the memo database
	in a C conduit.

	To build (from coldsync/conduits):
	
		gcc -g -o memo-conduit -I.. -I../include \
			-L../libpconn -L../libpdb \
			-lusb -lcap memo-conduit.c -lpconn

	christophe.beauregard@sympatico.ca
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>

#include <pconn/spc_client.h>
#include <pconn/dlp_cmd.h>

static void process_memo( PConnection* pconn ) {
	ubyte dbhandle;
	uword index;
	struct dlp_recinfo record;
	char* buf = NULL;
	struct dlp_opendbinfo dbinfo;
	int rc;

	rc = DlpOpenDB( pconn, 0, "MemoDB", DLPCMD_MODE_READ, &dbhandle );
	if( rc ) return;

	rc = DlpReadOpenDBInfo( pconn, dbhandle, &dbinfo );
	if( rc ) dbinfo.numrecs = 0;

	fprintf( stderr, "Reading %hu records\n", dbinfo.numrecs );

	for( index = 0; index < dbinfo.numrecs; index ++ ) {
		rc = DlpReadRecordByIndex( pconn, dbhandle, index, &record );
		if( rc ) break;

		rc = DlpReadRecordByID( pconn, dbhandle, record.id, 0, DLPC_RECORD_TOEND,
			&record, &buf );
		if( rc ) break;

		fprintf( stderr, "Memo %u (%hu bytes)\n%s\n\n\n",
			record.id, record.size, buf );
	}

	DlpCloseDB( pconn, dbhandle, 0 );
}

int main( int ac, char* av[] ) {
	PConnection* pconn = NULL;
	int spc_fd = -1;
	char* tmp;
	char buf[PATH_MAX];
	char* prefs[256];	/* arbitrary, but large enough */
	unsigned int nprefs = 0;
	char* headers[256];
	unsigned int nheaders = 0;

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
			"\ttype: memo/DATA\n"
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

	process_memo( pconn );

	printf( "201 Make a note\n" );
	exit( 0 );
}
