# ColdSync::SPC.pm
#
# Module for dealing with SPC requests from ColdSync conduits.
#
#	Copyright (C) 2000, Andrew Arensburger.
#	You may distribute this file under the terms of the Artistic
#	License, as specified in the README file.
#
# $Id: SPC.pm,v 1.21 2003-06-14 21:43:20 azummo Exp $

# XXX - Write POD

# XXX - Would be cool to do the following: the dbinfo doesn't change,
# so it can be cached the first time it's called; the subsequent
# times, just return what's in the cache.
#
# The vast majority of DLP requests will operate on the currently-open
# database. Make the database handle an optional argument. If omitted,
# defaults to the current database.
#
# Thus, the first time a DLP request function is sent, need to request
# dbinfo (and cache it), open that database, and cache the database
# handle for subsequent requests.
#
# To avoid database handle leaks and other problems, need to close the
# database when done. Doing this right would involve modifying
# ColdSync.pm to define "initializers" and "finalizers": arrays of
# closure references, much like BEGIN and END blocks, that get called
# before and after the body of the conduit runs.

use strict;
package ColdSync::SPC;

=head1 NAME

SPC - Allows ColdSync conduits to communicate directly with the Palm.

=head1 SYNOPSIS

    use ColdSync;
    use ColdSync::SPC;

=head1 DESCRIPTION

The SPC package includes functions for sending Serialized Request
Protocol (SPC) requests in ColdSync conduits.

=cut

use ColdSync;
use Exporter;

use vars qw( $VERSION @ISA *SPC @EXPORT %EXPORT_TAGS );

# One liner, to allow MakeMaker to work.
$VERSION = do { my @r = (q$Revision: 1.21 $ =~ /\d+/g); sprintf "%d."."%02d" x $#r, @r };

@ISA = qw( Exporter );

# Various useful constants
use constant SPCOP_NOP		=> 0;
use constant SPCOP_DBINFO	=> 1;
use constant SPCOP_DLPC		=> 2;
use constant SPCOP_DLPR		=> 3;

use constant SPCERR_OK		=> 0;
use constant SPCERR_BADOP	=> 1;
use constant SPCERR_NOMEM	=> 2;

use constant dlpRespErrNone	=> 0;
use constant dlpFirstArgID	=> 0x20;


use constant DLPCMD_ReadUserInfo			=> 0x10;
use constant DLPCMD_WriteUserInfo			=> 0x11;
use constant DLPCMD_ReadSysInfo				=> 0x12;
use constant DLPCMD_GetSysDateTime			=> 0x13;
use constant DLPCMD_SetSysDateTime			=> 0x14;
use constant DLPCMD_ReadStorageInfo			=> 0x15;
use constant DLPCMD_ReadDBList				=> 0x16;
use constant DLPCMD_OpenDB				=> 0x17;
use constant DLPCMD_CreateDB				=> 0x18;
use constant DLPCMD_CloseDB				=> 0x19;
use constant DLPCMD_DeleteDB				=> 0x1a;
use constant DLPCMD_ReadAppBlock			=> 0x1b;
use constant DLPCMD_WriteAppBlock			=> 0x1c;
use constant DLPCMD_ReadSortBlock			=> 0x1d;
use constant DLPCMD_WriteSortBlock			=> 0x1e;
use constant DLPCMD_ReadNextModifiedRec			=> 0x1f;
use constant DLPCMD_ReadRecord				=> 0x20;
use constant DLPCMD_WriteRecord				=> 0x21;
use constant DLPCMD_DeleteRecord			=> 0x22;
use constant DLPCMD_ReadResource			=> 0x23;
use constant DLPCMD_WriteResource			=> 0x24;
use constant DLPCMD_DeleteResource			=> 0x25;
use constant DLPCMD_CleanUpDatabase			=> 0x26;
use constant DLPCMD_ResetSyncFlags			=> 0x27;
use constant DLPCMD_CallApplication			=> 0x28;
use constant DLPCMD_ResetSystem				=> 0x29;
use constant DLPCMD_AddSyncLogEntry			=> 0x2a;
use constant DLPCMD_ReadOpenDBInfo			=> 0x2b;
use constant DLPCMD_MoveCategory			=> 0x2c;
use constant DLPCMD_ProcessRPC				=> 0x2d;
use constant DLPCMD_OpenConduit				=> 0x2e;
use constant DLPCMD_EndOfSync				=> 0x2f;
use constant DLPCMD_ResetRecordIndex			=> 0x30;
use constant DLPCMD_ReadRecordIDList			=> 0x31;
use constant DLPCMD_ReadNextRecInCategory		=> 0x32;
use constant DLPCMD_ReadNextModifiedRecInCategory	=> 0x33;
use constant DLPCMD_ReadAppPreference			=> 0x34;
use constant DLPCMD_WriteAppPreference			=> 0x35;
use constant DLPCMD_ReadNetSyncInfo			=> 0x36;
use constant DLPCMD_WriteNetSyncInfo			=> 0x37;
use constant DLPCMD_ReadFeature				=> 0x38;
use constant DLPCMD_FindDB				=> 0x39;
use constant DLPCMD_SetDBInfo				=> 0x3a;
use constant DLPCMD_LoopBackTest			=> 0x3b;
use constant DLPCMD_ExpSlotEnumerate			=> 0x3c;
use constant DLPCMD_ExpCardPresent			=> 0x3d;
use constant DLPCMD_ExpCardInfo				=> 0x3e;
use constant DLPCMD_VFSCustomControl			=> 0x3f;
use constant DLPCMD_VFSGetDefaultDirectory		=> 0x40;
use constant DLPCMD_VFSImportDatabaseFromFile		=> 0x41;
use constant DLPCMD_VFSExportDatabaseToFile		=> 0x42;
use constant DLPCMD_VFSFileCreate			=> 0x43;
use constant DLPCMD_VFSFileOpen				=> 0x44;
use constant DLPCMD_VFSFileClose			=> 0x45;
use constant DLPCMD_VFSFileWrite			=> 0x46;
use constant DLPCMD_VFSFileRead				=> 0x47;
use constant DLPCMD_VFSFileDelete			=> 0x48;
use constant DLPCMD_VFSFileRename			=> 0x49;
use constant DLPCMD_VFSFileEOF				=> 0x4a;
use constant DLPCMD_VFSFileTell				=> 0x4b;
use constant DLPCMD_VFSFileGetAttributes		=> 0x4c;
use constant DLPCMD_VFSFileSetAttributes		=> 0x4d;
use constant DLPCMD_VFSFileGetDates			=> 0x4e;
use constant DLPCMD_VFSFileSetDates			=> 0x4f;
use constant DLPCMD_VFSDirCreate			=> 0x50;
use constant DLPCMD_VFSDirEntryEnumerate		=> 0x51;
use constant DLPCMD_VFSGetFile				=> 0x52;
use constant DLPCMD_VFSPutFile				=> 0x53;
use constant DLPCMD_VFSVolumeFormat			=> 0x54;
use constant DLPCMD_VFSVolumeEnumerate			=> 0x55;
use constant DLPCMD_VFSVolumeInfo			=> 0x56;
use constant DLPCMD_VFSVolumeGetLabel			=> 0x57;
use constant DLPCMD_VFSVolumeSetLabel			=> 0x58;
use constant DLPCMD_VFSVolumeSize			=> 0x59;
use constant DLPCMD_VFSFileSeek				=> 0x5a;
use constant DLPCMD_VFSFileResize			=> 0x5b;
use constant DLPCMD_VFSFileSize				=> 0x5c;
use constant DLPCMD_ExpSlotMediaType			=> 0x5d;
use constant DLPCMD_WriteResourceStream		 	=> 0x5e;
use constant DLPCMD_WriteRecordStream			=> 0x5f;
use constant DLPCMD_ReadResourceStream			=> 0x60;
use constant DLPCMD_ReadRecordStream			=> 0x61;

%EXPORT_TAGS = (
	'dlp_vfs' => [ qw(
			DLPCMD_VFSCustomControl
			DLPCMD_VFSGetDefaultDirectory		
			DLPCMD_VFSImportDatabaseFromFile	
			DLPCMD_VFSExportDatabaseToFile		
			DLPCMD_VFSFileCreate			
			DLPCMD_VFSFileOpen			
			DLPCMD_VFSFileClose			
			DLPCMD_VFSFileWrite			
			DLPCMD_VFSFileRead			
			DLPCMD_VFSFileDelete			
			DLPCMD_VFSFileRename			
			DLPCMD_VFSFileEOF			
			DLPCMD_VFSFileTell			
			DLPCMD_VFSFileGetAttributes		
			DLPCMD_VFSFileSetAttributes		
			DLPCMD_VFSFileGetDates			
			DLPCMD_VFSFileSetDates			
			DLPCMD_VFSDirCreate			
			DLPCMD_VFSDirEntryEnumerate		
			DLPCMD_VFSGetFile			
			DLPCMD_VFSPutFile			
			DLPCMD_VFSVolumeFormat			
			DLPCMD_VFSVolumeEnumerate		
			DLPCMD_VFSVolumeInfo			
			DLPCMD_VFSVolumeGetLabel		
			DLPCMD_VFSVolumeSetLabel		
			DLPCMD_VFSVolumeSize			
			DLPCMD_VFSFileSeek			
			DLPCMD_VFSFileResize			
			DLPCMD_VFSFileSize			
	) ],

	'dlp_expslot' => [ qw(
			DLPCMD_ExpSlotEnumerate
			DLPCMD_ExpSlotMediaType
			DLPCMD_ExpCardPresent
			DLPCMD_ExpCardInfo
	) ],

	'dlp_args'=> [ qw(
			dlpRespErrNone
			dlpFirstArgID
	) ],
);

@EXPORT = qw( spc_req *SPC
	dlp_req
	spc_get_dbinfo
	spc_recv
	spc_send
	dlp_ReadSysInfo
	dlp_OpenDB
	dlp_CloseDB
	dlp_DeleteDB
	dlp_ReadAppBlock
	dlp_WriteAppBlock
	dlp_GetSysDateTime
	dlp_SetSysDateTime
	dlp_ReadOpenDBInfo
	dlp_CleanUpDatabase
	dlp_ReadUserInfo
	dlp_WriteUserInfo
	dlp_ReadRecordByIndex
	dlp_ReadRecordById
	dlp_AddSyncLogEntry
	dlp_DeleteRecord
	dlp_DeleteAllRecords
	dlp_WriteRecord
	dlp_SetDBInfo
	dlp_ResetRecordIndex
	dlp_ReadNextRecInCategory
	dlp_ReadNextModifiedRec
	dlp_ReadNextModifiedRecInCategory
	dlp_ReadDBList
);

Exporter::export_ok_tags('dlp_vfs', 'dlp_args', 'dlp_expslot');
		

# spc_init
# Initialize a conduit for SPC.
sub spc_init
{
	my $old_selected;	# Currently-selected file handle

	open SPC, "+>&$HEADERS{SPCPipe}" or die("Can't open SPC pipe");
	binmode SPC;		# For MS-DOS and friends

	# Make sure SPC is unbuffered
	$old_selected = select;
	select SPC;
	$| = 1;
	select $old_selected;
}

# spc_send
# Send an SPC request
sub spc_req
{
	my $op = shift;		# Opcode
	my $data = shift;	# Request data
	my $header;

	# Handle empty data
	$data = defined $data ? $data : "";

#	print "spc_req, Header:\n";
#	print sprintf "OP    : %02x\n", $op;

	# Send the SPC request: header and data
	$header = pack("n x2 N", $op, length($data));
	print SPC $header, $data;

	# Read the reply
	my $buf;
	my $status;		# Request status code
	my $len;		# Length of response data

	# Read the reply header
	read SPC, $buf, 8;	# XXX - Hardcoded constants are bad
	($op, $status, $len) = unpack("n n N", $buf);
	return undef if $status != 0;

#	print "spc_req, Response header:\n";
#	print sprintf "OP    : %02x\n", $op;
#	print sprintf "STATUS: %02x\n", $status;
#	print         "LEN   : $len\n";


	# Read the reply data
	if ($len > 0)
	{
		read SPC, $buf, $len;
	}

	return ($status, $buf, $len);
}

sub spc_recv
{
	my $wlen = shift;
	my $rlen = 0;
	my $gbuf = "";

#	print "spc_recv, reading $wlen bytes\n";

	while($rlen < $wlen)
	{
		my ($status, $buf, $len) = spc_req(SPCOP_DLPC);

#		print " - $len, $rlen\n";

		$rlen += $len;
		$gbuf .= $buf;
	}

	return $gbuf;
}

sub spc_send
{
	my $data = shift;

	spc_req(SPCOP_DLPC, $data);
}

=head1 FUNCTIONS

=head2 dlp_req

    ($err, @argv) = dlp_req($reqno, @args)

Sends a DLP request over SPC. C<$reqno> is the DLP request number;
C<@args> is the array of DLP arguments.

C<$err> is the DLP return status; C<@argv> is the array of values
returned from the Palm.

This is a fairly low-level function. It is much easier to use one of
the DLP wrapper functions. However, not every DLP function has been
implemented yet.

=cut

# dlp_req
# Send a DLP request over SPC.
#
# ($err, @argv) = dlp_req($reqno, @args)
# where
# $err is the return status,
# @argv are the (unparsed) returned arguments
# $reqno is the DLPCMD_* request number
# @args are the (unparsed) parameters
sub dlp_req
{
	my $cmd = shift;	# DLP command
	my @args = @_;		# All other arguments are DLP arguments

	my $dlp_req;		# DLP request data

	# Construct DLP header
	$dlp_req = pack("C C", $cmd, $#_+1);

	# Pack DLP arguments
	$dlp_req .= pack_dlp_args(@args);

	# Send it as an SPCOP_DLPC request
	my $status;
	my $data;
	($status, $data) = spc_req(SPCOP_DLPC, $dlp_req);

	return undef if !defined($status);

	# Read results

	my ($code, $argc, $errno) = unpack("C C n", $data);
			# $code should be $cmd | 0x80, but I'm not checking.

	$data = substr($data, 4);	# Just the arguments

	# Do minimal unpacking of return args into { id => X, data => Y }
	my @argv = unpack_dlp_args($argc, $data);

	return ($errno, @argv);
}

# pack_dlp_args
# Takes a set of arguments of the form
#	{ id => 123, data => "abc" }
# Packs each one as a DLP argument, concatenates them, and returns the
# result.
sub pack_dlp_args
{
	my $retval = "";

	foreach my $arg (@_)
	{
		if (length($arg->{data}) <= 0xff)
		{
			# Tiny argument
			# Make sure the high bits of the ID are 00
			$retval .= pack("C C", $arg->{id} & 0x3f,
					length($arg->{data})) .
					$arg->{data};
		} elsif (length($arg->{data}) <= 0xffff)
		{
			# Small argument
			# Make sure the high bits of the ID are 10
			$retval .= pack("C x n",
					($arg->{id} & 0x3f) | 0x80,
					length($arg->{data})) .
					$arg->{data};
		} else {
			# Long argument
			# Make sure the high bits of the ID are 11
			$retval .= pack("n N",
					$arg->{id} | 0xc000,
					length($arg->{data})) .
					$arg->{data};
		}
	}

	return $retval;
}

# unpack_dlp_args
# The reverse of pack_dlp_args: takes an argument count and a string
# of data, unpacks it into an array of elements of the form
#	{ id => 123, data => "abc" }
# and returns this array.
sub unpack_dlp_args
{
	my $argc = shift;
	my $data = shift;
	my @retval;

	# Set the length of @retval
	$#retval = $argc-1;

	# Unpack each argument in turn
	for (my $i = 0; $i < $argc; $i++)
	{
		my $id;			# Argument ID
		my $len;		# Length of argument data
		my $argdata;		# Argument data

		# Get the first byte of the string, and figure out the
		# size of the argument.
		# XXX - At this stage, should complain if $data has fewer
		# than 2 bytes.
		$id = unpack("C", $data);

		# XXX - In here, ought to complain if $data is shorter
		# than expected.
		if (($id & 0xc0) == 0xc0)
		{
			# Long argument
			($id, $len) = unpack("n N", $data);
			$id &= 0x3fff;
			$argdata = substr($data, 6, $len);
			$data = substr($data, 6+$len);

		} elsif (($id & 0xc0) == 0x80)
		{
			# Small argument
			($id, $len) = unpack("C x n", $data);
			$id &= 0x3f;
			$argdata = substr($data, 4, $len);
			$data = substr($data, 4+$len);

		} else {
			# Tiny argument
			($id, $len) = unpack("C C", $data);
			$id &= 0x3f;
			$argdata = substr($data, 2, $len);
			$data = substr($data, 2+$len);
		}

		$retval[$i] = {
			id	=> $id,
			data	=> $argdata,
		};
	}

	return @retval;
}

=head2 spc_get_dbinfo

	$dbinfo = spc_get_dbinfo();

Returns a reference to a hash containing information about the
database currently being synchronized:

	$dbinfo->{size}
	$dbinfo->{misc_flags}
	$dbinfo->{db_flags}
	$dbinfo->{type}
	$dbinfo->{creator}
	$dbinfo->{version}
	$dbinfo->{modnum}
	%{$dbinfo->{ctime}}
	%{$dbinfo->{mtime}}
	%{$dbinfo->{baktime}}
	$dbinfo->{db_index}
	$dbinfo->{name}

The C<size> field indicates the size of the database information
block, and is not at all useful here.

The C<creator> and C<type> fields are four-character strings
specifying the creator and type of the databse.

The C<version> field indicates the version of the database.

The C<modnum> field indicates the modification number of the database.
Each time the database is modified, this number is incremented. Since
there is no mechanism within ColdSync to keep this number consistent
between the Palm and the desktop, this number is not very useful.

The C<name> field gives the name of the database.

C<$dbinfo-E<gt>{ctime}>, C<$dbinfo-E<gt>{mtime}>, and
C<$dbinfo-E<gt>{baktime}> indicate the creation, modification, and
backup time of the database. They are all of the following form:

	$dbinfo->{ctime}{year}
	$dbinfo->{ctime}{month}
	$dbinfo->{ctime}{day}
	$dbinfo->{ctime}{hour}
	$dbinfo->{ctime}{minute}
	$dbinfo->{ctime}{second}

=cut
# spc_get_dbinfo
# Convenience function to read dbinfo information about current database.
# Returns a hash with the dbinfo information, or undef in case of error.
sub spc_get_dbinfo
{
	my $retval = {};
	my $status;
	my $data;

	# Send a dbinfo SPC request
	($status, $data) = spc_req(SPCOP_DBINFO, undef);
	return undef unless $status == SPCERR_OK;

	# Now parse the results
	($retval->{size},
	 $retval->{misc_flags},
	 $retval->{db_flags},
	 $retval->{type},
	 $retval->{creator},
	 $retval->{version},
	 $retval->{modnum},
	 $retval->{ctime}{year},
	 $retval->{ctime}{month},
	 $retval->{ctime}{day},
	 $retval->{ctime}{hour},
	 $retval->{ctime}{minute},
	 $retval->{ctime}{second},
	 $retval->{mtime}{year},
	 $retval->{mtime}{month},
	 $retval->{mtime}{day},
	 $retval->{mtime}{hour},
	 $retval->{mtime}{minute},
	 $retval->{mtime}{second},
	 $retval->{baktime}{year},
	 $retval->{baktime}{month},
	 $retval->{baktime}{day},
	 $retval->{baktime}{hour},
	 $retval->{baktime}{minute},
	 $retval->{baktime}{second},
	 $retval->{db_index},
	 $retval->{name},
	) = unpack("C C n a4 a4 n N nCCCCCx nCCCCCx nCCCCCx n a*", $data);

	# XXX - Consider parsing db_flags and misc_flags further (make
	# them hashes with keys named after flags, with boolean
	# values.

	# XXX - Need to check what happens to baktime if the db has
	# never been synced. Maybe year will be 0 ?

	return $retval;
}

=head2 dlp_ReadSysInfo

	$sysinfo = dlp_ReadSysInfo();

Returns a reference to a hash containing information about the Palm:

	$sysinfo->{ROM_version}
	$sysinfo->{localization_ID}
	$sysinfo->{product_ID}
The C<ROM_version> field indicates the version of the Palm's ROM.

I don't know what the C<localization_ID> field is.

The C<product_ID> indicates which particular model of Palm this is.

The following fields may not be returned by all Palms:

	$sysinfo->{DLP_version}
	$sysinfo->{compat_version}
	$sysinfo->{maxrec}

The C<DLP_version> indicates the version of the DLP protocol that the
Palm implements.

I'm not sure what the C<compat_version> field is. I suspect that it
gives the earliest version of DLP with which this Palm is compatible.

The C<maxrec> field indicates the maximum size of a record or
resource.

=cut
#'

use constant dlpReadSysInfoVerRespArgID	=> 0x21;

sub dlp_ReadSysInfo
{
	my $retval;

	# Send the request
	my ($err, @argv) = dlp_req(DLPCMD_ReadSysInfo);

	return undef unless defined $err;

	# Unpack the arguments further

	$retval = {};
	foreach my $arg (@argv)
	{
		if ($arg->{id} == dlpFirstArgID)
		{
			my $rom_version;
			my $localizationID;
			my $prodIDsize;
			my $prodID;

			($rom_version, $localizationID, $prodIDsize, $prodID) =
				unpack("N N x C N", $arg->{data});
			$retval->{"ROM_version"} = $rom_version;
			$retval->{"localization_ID"} = $localizationID;
			$retval->{"product_ID"} = $prodID;

		} elsif ($arg->{id} == dlpReadSysInfoVerRespArgID)
		{
			my $dlpVmaj;
			my $dlpVmin;
			my $compVmaj;
			my $compVmin;
			my $maxrec;

			($dlpVmaj, $dlpVmin, $compVmaj, $compVmin, $maxrec) =
				unpack("n n n n N", $arg->{data});
			$retval->{"DLP_version"} = "$dlpVmaj.$dlpVmin";
			$retval->{"compat_version"} =
				"$compVmaj.$compVmin";
			$retval->{"maxrec"} = $maxrec;

		} else {
			# XXX - What to do?
			# print STDERR "Unknown arg ID\n";
		}
	}

	return $retval;
}

=head2 dlp_OpenDB

	$dbh = dlp_OpenDB($dbname, $mode);

Opens a database on the Palm. C<$dbname> is a string indicating the
name of the database; the name of the current database can be gotten
from C<spc_get_dbinfo>.

C<$mode> indicates how to open the database. It is the bitwise-or of
any of the following values:

	0x80		Open for reading
	0x40		Open for writing
	0x20		Exclusive access
	0x10		Show secret records

If successful, C<dlp_OpenDB> returns a database handle: a small
integer that refers to the database that was just opened. The database
handle will be passed to various other functions that operate on the
database.

If unsuccessful, C<dlp_OpenDB> returns C<undef>.

=cut
sub dlp_OpenDB
{
	my $dbname = shift;	# Name of database to open
	my $mode = shift;	# Mode
				# XXX - For now, numeric. Be nice to allow
				# words or something
	my $cardNo = shift;	# (optional) Memory card number

	# Sanity checks on arguments
	$dbname = substr($dbname, 0, 31);
				# XXX - Hard-coded constants are bad, m'kay?
	$mode &= 0xff;
	$cardNo = 0 if !defined($cardNo);

	# Send the request

	my ($err, @argv) = dlp_req(DLPCMD_OpenDB,
			{
				id	=> dlpFirstArgID,
				data	=> pack("C C a*x",
					$cardNo, $mode, $dbname),
			});

	return undef unless defined $err;

	# Parse the return arguments
	my $retval;

	for (@argv)
	{
		if ($_->{id} == dlpFirstArgID)
		{
			$retval = unpack("C", $_->{data});
		} else {
			# XXX - Now what? Barf or something?
		}
	}

	return $retval;
}

=head2 dlp_CloseDB

	dlp_CloseDB($dbh);

Closes the database associated with the database handle C<$dbh> (see
L</dlp_OpenDB>).

=cut
#'
sub dlp_CloseDB
{
	my $dbh = shift;	# Database handle

	my ($err, @argv) = dlp_req(DLPCMD_CloseDB,
			{
				id	=> dlpFirstArgID,
				data	=> pack("C", $dbh),
			});

	return $err;
}

=head2 dlp_DeleteDB

	dlp_DeleteDB($dbh);

Deletes the database associated with the database handle C<$dbh> (see
L</dlp_OpenDB>).

=cut
#'
sub dlp_DeleteDB
{
	my $cardno = shift;	# Card number
	my $dbname = shift;	# Database name

	my ($err, @argv) = dlp_req(DLPCMD_DeleteDB,
			{
				id	=> dlpFirstArgID,
				data	=> pack("C x Z*", $cardno, $dbname),
			});

	return $err;
}

=head2 dlp_ReadOpenDBInfo

	$dbrecords = dlp_ReadOpenDBInfo($dbh);

Reads the info from the current database associated with the database
handle C<$dbh> and returns a reference to a hash containing
information about the database:

	$dbrecords->{numrecords}

Returns the number of records in the database.

=cut
#'
sub dlp_ReadOpenDBInfo
{
	my $dbh = shift;	# Database handle

	my ($err, @argv) = dlp_req(DLPCMD_ReadOpenDBInfo,
			{
				id	=> dlpFirstArgID,
				data	=> pack("C", $dbh),
			});

	return undef unless defined $err;

	# Parse the return arguments
	my $retval = {};
	

	# Default values
	$retval->{'numrecords'} = undef;

	for (@argv)
	{
		if ($_->{id} == dlpFirstArgID)
		{
			$retval->{'numrecords'} = unpack("n", $_->{data});
		} else {
			# Maybe this command will be extended in a future DLP
			# version.
		}
	}

	return $retval;
}

=head2 dlp_CleanUpDatabase

	dlp_CleanUpDatabase($dbh);

=cut
#'
sub dlp_CleanUpDatabase
{
	my $dbh = shift;	# Database handle

	my ($err, @argv) = dlp_req(DLPCMD_CleanUpDatabase,
			{
				id	=> dlpFirstArgID,
				data	=> pack("C", $dbh),
			});

	return $err;
}

=head2 dlp_AddSyncLogEntry

	dlp_AddSyncLogEntry($text);

Adds the entry C<$text> in the sync log for the database currently being
synched.

=cut
#'
sub dlp_AddSyncLogEntry
{
	my $text = shift;	# Text to add to the log file

	my ($err, @argv) = dlp_req(DLPCMD_AddSyncLogEntry,
			{
				id	=> dlpFirstArgID,
				data	=> pack("Z*", $text),
			});

	return $err;
}

=head2 dlp_ReadUserInfo

	$user = dlp_ReadUserInfo();

Returns a reference to a hash containing information about the user
info.

	$user->{'userid'}
The Palm user id.

	$user->{'viewer'}
I don't know what this is.

	$user->{'lastsyncpc'}
The last pc that the Palm synced with.

	$user->{succsyncdate}{year}
	$user->{succsyncdate}{month}
	$user->{succsyncdate}{day}
	$user->{succsyncdate}{hour}
	$user->{succsyncdate}{minute}
	$user->{succsyncdate}{second}
The last successful sync date, broken down into year, month, day,
hour, minute, second.

	$user->{lastsyncdate}{year}
	$user->{lastsyncdate}{month}
	$user->{lastsyncdate}{day}
	$user->{lastsyncdate}{hour}
	$user->{lastsyncdate}{minute}
	$user->{lastsyncdate}{second}
The last sync date, broken down into year, month, day, hour, minute,
second. If this does not match the last successful sync date then the
last attempt to sync failed.

	$user->{'usernamelen'}
	$user->{'passwordlen'}
The number of characters in the username and the number of characters
in the Palm password.

	$user->{'username'}
	$user->{'password'}
The username and passwrod that is used for syncing.

=cut
#'
sub dlp_ReadUserInfo
{
	my ($err, @argv) = dlp_req(DLPCMD_ReadUserInfo);

	return undef unless defined $err;

	# Parse the return arguments
	my $retval = {};
	

	# Some defaults

	$retval->{"clastsyncdate"} = undef;
	$retval->{"csuccsyncdate"} = undef;
	

	for (@argv)
	{
		if ($_->{id} == dlpFirstArgID)
		{
			# XXX - hardcoded constant. We should complain
			# if there are fewer than 30 bytes.
			# XXX - No, this is Perl. Cope. Pad to 30 bytes
			# with NULs if necessary.

			my($header,$nameandpass) =
				unpack("a30 a*", $_->{'data'});

			($retval->{'userid'},
			 $retval->{'viewer'},
			 $retval->{'lastsyncpc'},

			 $retval->{succsyncdate}{year},
			 $retval->{succsyncdate}{month},
			 $retval->{succsyncdate}{day},
			 $retval->{succsyncdate}{hour},
			 $retval->{succsyncdate}{minute},
			 $retval->{succsyncdate}{second},

			 $retval->{lastsyncdate}{year},
			 $retval->{lastsyncdate}{month},
			 $retval->{lastsyncdate}{day},
			 $retval->{lastsyncdate}{hour},
			 $retval->{lastsyncdate}{minute},
			 $retval->{lastsyncdate}{second},

			 # XXX - These are bogus: this is Perl, so
			 # there's no need to store the length of
			 # anything. Don't return it. If the caller
			 # cares, he can measure it again.
			 $retval->{'usernamelen'},
			 $retval->{'passwordlen'},

			) = unpack("N N N nCCCCCx nCCCCCx C C", $header);

			# Let's check if the date is valid
			undef $retval->{succsyncdate}
				if $retval->{succsyncdate}{year} eq 0;
			undef $retval->{lastsyncdate}
				if $retval->{lastsyncdate}{year} eq 0;

			# Decode username and password

			# XXX - usernamelen is 0 when there's no
			# username. Otherwise, it represents the
			# length of the user name (doh!), including
			# the trailing NUL. So, when usernamelen is eq
			# 1, there's only the NUL.

			# It's correct to set the username to undef in
			# this case?

			if ($retval->{'usernamelen'} > 1)
			{
				my $len = $retval->{'usernamelen'} - 1;
						# Discard trailing NUL

				$retval->{'username'} =
					unpack("a$len", $nameandpass);
			}
			else
			{
				$retval->{'username'} = undef;
			}

			# XXX - I had some problems when
			# syncing a Palm with a password, so this
			# section has not been tested.
			if ($retval->{'passwordlen'} ne 0)
			{
				my $unpackstring = "x$retval->{'usernamelen'}"
					. "a$retval->{'passwordlen'}";

				$retval->{'password'} =
					unpack($unpackstring, $nameandpass);
			}
			else
			{
				$retval->{'password'} = undef;
			}
		}
	}

	return $retval;
}

sub dlp_WriteUserInfo
{
	my $ui = shift;	# User info hash reference
			# See dlp_ReadUserInfo for the format. If you don't
			# want to modify a value, don't put it in the hash.

	my $modflags = 0;

	# Some useful constants

	use constant dlpUserInfoModUserID	=> 0x80;  # changing the user id
	use constant dlpUserInfoModSyncPC	=> 0x40;  # changing the last sync PC id
	use constant dlpUserInfoModSyncDate	=> 0x20;  # changing sync date
	use constant dlpUserInfoModName		=> 0x10;  # changing user name
	use constant dlpUserInfoModViewerID	=> 0x08;  # changing the viewer id

	# Check what values we should modify.

	$modflags |= dlpUserInfoModName		if defined $ui->{'username'};
	$modflags |= dlpUserInfoModViewerID	if defined $ui->{'viewer'};
	$modflags |= dlpUserInfoModUserID	if defined $ui->{'userid'};
	$modflags |= dlpUserInfoModSyncPC	if defined $ui->{'lastsyncpc'};
#	$modflags |= dlpUserInfoModSyncDate	if defined $ui->{'lastsyncdate'};

	# XXX - Changing the last sync date is not (yet) supported.

	# Adjust undefined values.

	$ui->{'userid'}		= $ui->{'userid'} or 0;
	$ui->{'viewer'}		= $ui->{'viewer'} or 0;
	$ui->{'lastsyncpc'}	= $ui->{'lastsyncpc'} or 0;
	$ui->{'username'}	= defined $ui->{'username'} ? $ui->{'username'} : '';
	$ui->{'usernamelen'}	= length $ui->{'username'} + 1;

	# XXX - Check for the maximum length of an username.

	my ($err, @argv) = dlp_req(DLPCMD_WriteUserInfo,
			{
				id	=> dlpFirstArgID,
				data	=> pack("N N N xxxxxxxx C C Z$ui->{'usernamelen'}",
						$ui->{'userid'},
						$ui->{'viewer'},
						$ui->{'lastsyncpc'},
						$modflags,
						$ui->{'usernamelen'},
						$ui->{'username'}
				),
			});

	return $err;
}


=head2 dlp_DeleteRecord

	dlp_DeleteRecord($dbh, $recordid, $flags);

Deletes one record of the database associated with the database handle
C<$dbh>.

The $recordid is the id of the record to delete.

I don't know what $flags does (you can omit it for now).

=cut
#'
sub dlp_DeleteRecord
{
	my $dbh		= shift;	# Database handle
	my $recordid	= shift;	# Id of the record to delete
	my $flags	= shift;	# Flags. Set to 0 for normal use.


	$flags = 0 unless defined $flags;

	my ($err, @argv) = dlp_req(DLPCMD_DeleteRecord,
			{
				id	=> dlpFirstArgID,
				data	=> pack("C C N",
						$dbh, $flags, $recordid),
			});

	return $err;
}

=head2 dlp_DeleteAllRecords

	dlp_DeleteAllRecords($dbh);

Deletes all the records from the database associated with the database
handle C<$dbh>.

=cut
#'
sub dlp_DeleteAllRecords
{
	my $dbh = shift;	# Database handle

	return _dlp_DeleteRecord($dbh, 0, 0x80);
				# XXX - YAHC, Yet Another Hard coded Constant
}

# unpack a record received by all the various dlp_Read*
# and setup the attributes hash
sub _unpackRecord {
	my $retval = {};
	for (@_) {
		if ($_->{'id'} == dlpFirstArgID) {
			@$retval{
			 'id',
			 'index',
			 'size',
			 'attrs',
			 'category',
			 'data',
			} = unpack("N n n C C a*", $_->{'data'});

			$retval->{'attributes'}{'deleted'} =
				$retval->{'attrs'} & 0x80 ? 1 : 0;

			$retval->{'attributes'}{'dirty'} =
				$retval->{'attrs'} & 0x40 ? 1 : 0;

			$retval->{'attributes'}{'busy'} =
				$retval->{'attrs'} & 0x20 ? 1 : 0;

			$retval->{'attributes'}{'secret'} =
				$retval->{'attrs'} & 0x10 ? 1 : 0;

			$retval->{'attributes'}{'archived'} =
				$retval->{'attrs'} & 0x08 ? 1 : 0;

			my $recsize = length($retval->{'data'});

			# XXX - This is untested. 

#			die("dlp_ReadRecord: Bad record size. Expected: $retval->{'size'}, Got: $recsize .\n")
#				unless $retval->{'size'} == $recsize;
		}
	}

	# needed by the dlp_ReadNext* methods
	return undef unless defined $retval->{id};

	return $retval;
}

=head2 dlp_ReadRecordById

	$record = dlp_ReadRecordById($dbh, $recordid, $offset, $numbytes);

Returns a reference to a hash containing information about the record
specified by C<$id> for the database associated with the database
handle C<$dbh>.

C<$offset> is the offset at which to begin reading, relative to the
beginning of the record, with 0 being the beginning of the record.
C<$numbytes> is the number of characters to read.

	$record->{'id'}
	$record->{'index'}
	$record->{'size'} 
	$record->{'attrs'}
	$record->{'category'}
	$record->{'data'}
	$record->{'attributes'}{'deleted'}
	$record->{'attributes'}{'dirty'}  
	$record->{'attributes'}{'busy'}   
	$record->{'attributes'}{'secret'} 
	$record->{'attributes'}{'archived'}

=cut
#'

sub dlp_ReadRecordById
{
	my $dbh		= shift;	# Database handle
	my $id		= shift;	# Record id
	my $offset	= shift;	# Offset into the record
	my $numbytes	= shift;	# Number of bytes to read starting at 
					# the offset (-1 = "to the end")

	return _dlp_ReadRecord(1, $dbh, $id, $offset, $numbytes);
}




=head2 dlp_ReadRecordByIndex

	$record = dlp_ReadRecordByIndex($dbh, $recordindex, $offset, $numbytes);

Returns a reference to a hash containing information about the record
specified by C<$recordindex> for the database associated with the
database handle C<$dbh>.

C<$offset> is the offset at which to begin reading, relative to the
beginning of the record, with 0 being the beginning of the record.
C<$numbytes> is the number of characters to read.

	$record->{'id'}
	$record->{'index'}
	$record->{'size'} 
	$record->{'attrs'}
	$record->{'category'}
	$record->{'data'}
	$record->{'attributes'}{'deleted'}
	$record->{'attributes'}{'dirty'}  
	$record->{'attributes'}{'busy'}   
	$record->{'attributes'}{'secret'} 
	$record->{'attributes'}{'archived'}

=cut
#'

sub dlp_ReadRecordByIndex
{
	my $dbh		= shift;	# Database handle
	my $index	= shift;	# Record index
	my $offset	= shift;	# Offset into the record
	my $numbytes	= shift;	# Number of bytes to read starting at
					# the offset (-1 = "to the end")

	return _dlp_ReadRecord(0, $dbh, $index, $offset, $numbytes);
}

sub _dlp_ReadRecord
{
	my $readbyid	= shift;	# Read record (1: by id, 0: by index)
	my $dbh		= shift;	# Database handle
	my $idindex	= shift;	# Record index (or record id)
	my $offset	= shift;	# Offset into the record

	my $numbytes	= shift;	# Number of bytes to read starting at
					# the offset (-1 = "to the end")

	$offset		= 0 unless defined $offset;
	$numbytes	= -1 unless defined $numbytes;

	my ($err, @argv) = dlp_req(DLPCMD_ReadRecord,
			{
				id	=> $readbyid ? 0x20 : 0x21,
				data	=> pack("C x n n n",
						$dbh, $idindex, $offset,
						$numbytes),
			});

	return undef unless defined $err;

	# XXX - I think there should be a way for the caller
	# to retrieve the original error code. Any idea?
	# (Should be implemented coherently in every dlp
	# function) - az.

	return _unpackRecord(@argv);
}

=head2 dlp_ResetRecordIndex

	dlp_ResetRecordIndex($dbh);

Resets the modified index.
=cut
#'
sub dlp_ResetRecordIndex($) {
	my $dbh = shift;
	my ($err, @argv) = dlp_req( DLPCMD_ResetRecordIndex,
		{
			'id' => dlpFirstArgID,
			'data' => pack("C", $dbh),
		});
}

=head2 dlp_ReadNextModifiedRec

	$record = dlp_ReadNextModifiedRec($dbh);

Returns a reference to a hash containing information about the next
modified record in the database (since last sync). Fields returned
are the same as dlp_ReadRecord. Returns undef when no more modified records
are available.

=cut
#'
sub dlp_ReadNextModifiedRec($) {
	my $dbh = shift;
	my ($err, @argv) = dlp_req(DLPCMD_ReadNextModifiedRec,
		{
			'id' => dlpFirstArgID,
			'data' => pack("C", $dbh),
		});

	# err is non zero when we reach the last record
	return undef unless defined $err;
	return undef unless $err != 0;

	return _unpackRecord(@argv);
}

=head2 dlp_ReadNextRecInCategory

	$record = dlp_ReadNextRecInCategory($dbh,$catno);

Returns a reference to a hash containing information about the next
record in the database matching the specified category.
Fields returned are the same as dlp_ReadRecord. Returns undef when no
more records are available.

=cut
#'
sub dlp_ReadNextRecInCategory($$) {
	my ($dbh,$catno) = @_;
	my ($err, @argv) = dlp_req(DLPCMD_ReadNextRecInCategory,
		{
			'id' => dlpFirstArgID,
			'data'  => pack("C C", $dbh, $catno)
		});

	# err is non zero when we reach the last record
	return undef unless defined $err;
	return undef unless $err != 0;

	return _unpackRecord(@argv);
}

=head2 dlp_ReadNextModifiedRecInCategory

	$record = dlp_ReadNextModifiedRecInCategory($dbh,$catno);

Returns a reference to a hash containing information about the next
modified record in the database matching the specified category.
Fields returned are the same as dlp_ReadRecord. Returns undef when no
more modified records are available.

=cut
#'
sub dlp_ReadNextModifiedRecInCategory($$) {
	my ($dbh,$catno) = @_;
	my ($err, @argv) = dlp_req(DLPCMD_ReadNextModifiedRecInCategory,
		{
			'id' => dlpFirstArgID,
			'data'  => pack("C C", $dbh, $catno)
		});

	# err is non zero when we reach the last record
	return undef unless defined $err;
	return undef unless $err != 0;

	return _unpackRecord(@argv);
}

sub dlp_WriteRecord
{
	my($dbh,	# Database handle
	   $id,		# Record id
	   $data,	# Record data
	   $category,	# Record category
	   $attrs,	# Record attributes
	   $flags	# Transaction flags (?)
	) = @_;

	$category	= 0x00 unless defined $category;
	$attrs		= 0x00 unless defined $attrs;
	$flags		= 0x80 unless defined $flags; # XXX - YAHC

	my ($err, @argv) = dlp_req(DLPCMD_WriteRecord,
			{
				id	=> dlpFirstArgID,
				data	=> pack("C C N C C a*",
						$dbh, $flags, $id, $attrs,
						$category, $data),
			});

	return undef unless defined $err;

	my $retval = {};

	for (@argv)
	{
		if ($_->{id} == dlpFirstArgID)
		{
			$retval->{'newid'} = unpack("N", $_->{data});
		}
	}

	return ($err, $retval, $retval->{'newid'});
}



=head2 dlp_ReadAppBlock

	$appinfo = dlp_ReadAppBlock($dbh [, $offset, $len]);

Reads the AppInfo block of the database associated with database
handle C<$dbh> (see L</dlp_OpenDB>).

C<$offset> is an integer specifying an offset from the beginning of
the AppInfo block at which to start reading. C<$len> is the length of
the data to return.

If omitted, C<$offset> defaults to 0 (read from the beginning) and
C<$len> defaults to -1 (read to the end).

The value returned by C<dlp_ReadAppBlock> is a string of binary data.
It is not parsed in any way.

=cut
#'
sub dlp_ReadAppBlock
{
	my $dbh		= shift;	# Database handle
	my $offset	= shift;	# Offset into AppInfo block. Optional,
					# defaults to 0
	my $len		= shift;	# bytes to read. Optional, defaults
					# to "to end of AppInfo block".

	$offset	= 0	unless defined $offset;
	$len	= -1	unless defined $len;

	my ($err, @argv) = dlp_req(DLPCMD_ReadAppBlock,
			{
				id	=> dlpFirstArgID,
				data	=> pack("Cx n n",
						$dbh, $offset, $len),
			});

	return undef unless defined $err;

	# Parse the return arguments
	my $retval;

	for (@argv)
	{
		if ($_->{id} == dlpFirstArgID)
		{
			my $dummy;

			$retval = substr($_->{data}, 2);
		} else {
			# XXX - Now what? Barf or something?
		}
	}

	return $retval;
}

=head2 dlp_WriteAppBlock

	$err = dlp_WriteAppBlock($dbh, $appinfo);

Writes a new AppInfo block to the database indicated by database
handle C<$dbh>. C<$appinfo> is a string of binary data, the raw
AppInfo block; it is not parsed in any way.

Returns the DLP error code.

=cut
#'
sub dlp_WriteAppBlock
{
	my $dbh		= shift;	# Database handle
	my $data	= shift;	# AppInfo block to upload

	my ($err, @argv) = dlp_req(DLPCMD_WriteAppBlock,
			{
				id	=> dlpFirstArgID,
				data	=> pack("Cx n a*",
						$dbh, length($data),
						$data),
			});
	# No return arguments to parse
	return $err;
}

=head2 dlp_GetSysDateTime

	$datetime = dlp_GetSysDateTime();

Reads the date and time from the Palm. Returns a reference to a hash
containing the date, with fields
"year", "month", "day", "hour", "minute", "second".

=cut

sub dlp_GetSysDateTime
{
	my ($err, @argv) = dlp_req(DLPCMD_GetSysDateTime);

	return undef unless defined $err;

	my $retval = {};

	foreach my $arg (@argv) {
		if ($arg->{id} == dlpFirstArgID) {
			@$retval{"year", "month", "day",
				"hour", "minute", "second"} =
			unpack("nC5",$arg->{data});
		}
	}

	return $retval;
}

=head2 dlp_SetSysDateTime

	$err = dlp_SetSysDateTime($year, $mon, $day, $hour, $min, $sec);

Sets the time on the Palm as indicated.

C<$year> must be a 4 digit number, and is not 1900 subtracted, as
returned from localtime(). C<$mon> must be a 1-offset number
(January = 1).

The first argument may be a reference to an hash, using the same
format as the one returned by dlp_GetSysDataTime .

Returns the DLP error code.

=cut

sub dlp_SetSysDateTime
{

	my $year	= shift;
	my $mon		= shift;
	my $day		= shift;
	my $hour	= shift;
	my $min		= shift;
	my $sec		= shift;

	if (ref($year) eq 'HASH')
	{
		my %newtime = %$year;

		(
		 $year, $mon, $day,
		 $hour, $min, $sec
		) = @newtime{'year', 'month', 'day', 'hour', 'minute',
				'second'};
	}

	my ($err, @argv) = dlp_req(DLPCMD_SetSysDateTime,
				 {
					 id   => dlpFirstArgID,
					 data => pack("nC5x",
						      $year, $mon, $day,
						      $hour, $min, $sec)
				 }
				 );

	# No return arguments to parse
	return $err;
}

sub _dlpdatezero
{
	my %date;

	$date{'year'}	= 0;
	$date{'month'}	= 0;
	$date{'day'}	= 0;
	$date{'hour'}	= 0;
	$date{'minute'} = 0;
	$date{'second'} = 0;

	return \%date;
}

sub dlp_SetDBInfo
{
	my $dbh		= shift; # db handle		
	my $dbinfo	= shift; # Hash with infos to set.

	# Check given values and set defaults.

	$dbinfo->{'wClrDbFlags'}	= 0x0000 unless defined $dbinfo->{'wClrDbFlags'};
	$dbinfo->{'wSetDbFlags'}	= 0x0000 unless defined $dbinfo->{'wSetDbFlags'};
	$dbinfo->{'wDbVersion'}		= 0xFFFF unless defined $dbinfo->{'wDbVersion'};

	$dbinfo->{'crDate'}	= _dlpdatezero() unless defined $dbinfo->{'crDate'}{'year'};
	$dbinfo->{'modDate'}	= _dlpdatezero() unless defined $dbinfo->{'modDate'}{'year'};
	$dbinfo->{'bckUpDate'}	= _dlpdatezero() unless defined $dbinfo->{'bckUpDate'}{'year'};

	$dbinfo->{'dwType'}	= "\0\0\0\0" unless defined $dbinfo->{'dwType'};
	$dbinfo->{'dwCreator'}	= "\0\0\0\0" unless defined $dbinfo->{'dwCreator'};

	# This one must always be null terminated.
	$dbinfo->{'name'}	= "\0\0" unless defined $dbinfo->{'name'};

	my ($err, @argv) = dlp_req(DLPCMD_SetDBInfo,
				 {
					 id   => dlpFirstArgID,
					 data => pack("C x n n n nCCCCCx nCCCCCx nCCCCCx a4 a4 a*",
						$dbh,
						$dbinfo->{'wClrDbFlags'},
						$dbinfo->{'wSetDbFlags'},
						$dbinfo->{'wDbVersion'},
						$dbinfo->{'crDate'}{'year'},
						$dbinfo->{'crDate'}{'month'},
						$dbinfo->{'crDate'}{'day'},
						$dbinfo->{'crDate'}{'hour'},
						$dbinfo->{'crDate'}{'minute'},
						$dbinfo->{'crDate'}{'second'},
						$dbinfo->{'modDate'}{'year'},
						$dbinfo->{'modDate'}{'month'},
						$dbinfo->{'modDate'}{'day'},
						$dbinfo->{'modDate'}{'hour'},
						$dbinfo->{'modDate'}{'minute'},
						$dbinfo->{'modDate'}{'second'},
						$dbinfo->{'bckUpDate'}{'year'},
						$dbinfo->{'bckUpDate'}{'month'},
						$dbinfo->{'bckUpDate'}{'day'},
						$dbinfo->{'bckUpDate'}{'hour'},
						$dbinfo->{'bckUpDate'}{'minute'},
						$dbinfo->{'bckUpDate'}{'second'},
						$dbinfo->{'dwType'},
						$dbinfo->{'dwCreator'},
						$dbinfo->{'name'},
					),
				 }
				 );

	# No return arguments to parse
	return $err;
}

=head2 dlp_ReadDBList

  my $start = 0;
  while(my $dbinfo = dlp_ReadDBList($card, $flags, $start)) {
    print $dbinfo->{'name'}, "\n";
    $start = $dbinfo->{'last_index'} + 1;
  }

Get the database list from the Palm. C<$card> indicates the card from which
to get the list. C<$start> indicates the index at which to start. C<$flags>
indicates where to get the database. It's a bitwise mask of the following:

	0x80 from RAM
	0x40 from ROM

The multi mask (0x20) is not supported.

The function is called repeatedly, each time adjusting C<$start> to the
next value after the C<last_index> field of the returned hash reference.
When no more databases are available, undef is returned.

Successful calls return a hash reference containing:

	$dbinfo->{size}
	$dbinfo->{misc_flags}
	$dbinfo->{db_flags}
	$dbinfo->{type}
	$dbinfo->{creator}
	$dbinfo->{version}
	$dbinfo->{modnum}
	%{$dbinfo->{ctime}}
	%{$dbinfo->{mtime}}
	%{$dbinfo->{baktime}}
	$dbinfo->{db_index}
	$dbinfo->{name}

Note that this is a fairly expensive function in terms of communications
time. It should be used sparingly and the results, where possible, should
be cached.

=cut
sub dlp_ReadDBList($$$)
{
	my ($card, $flags, $start) = @_;
	my $retval = {};

	# XXX - need to filter out multi. While there's no reason it shouldn't
	# work, it doesn't seem to fit into the existing
	# return-one-object-at-a-time API.
	# XXX - and get rid of the magic constants
	$flags &= 0x40|0x80;

	my ($err, @argv) = dlp_req(DLPCMD_ReadDBList,
		{
			'id' => dlpFirstArgID,
			'data' => pack("C C n", $flags, $card, $start),
		});
	return undef unless defined $err or $err != 0;

	foreach (@argv) {
		if($_->{'id'} == dlpFirstArgID) {
			next unless length $_->{'data'} > 10;	# arbitrary, but enough
			my $num = 0;
			($retval->{last_index},
			 $retval->{oflags},
			 $num,
			 $retval->{size},
			 $retval->{misc_flags},
			 $retval->{db_flags},
			 $retval->{type},
			 $retval->{creator},
			 $retval->{version},
			 $retval->{modnum},
			 $retval->{ctime}{year},
			 $retval->{ctime}{month},
			 $retval->{ctime}{day},
			 $retval->{ctime}{hour},
			 $retval->{ctime}{minute},
			 $retval->{ctime}{second},
			 $retval->{mtime}{year},
			 $retval->{mtime}{month},
			 $retval->{mtime}{day},
			 $retval->{mtime}{hour},
			 $retval->{mtime}{minute},
			 $retval->{mtime}{second},
			 $retval->{baktime}{year},
			 $retval->{baktime}{month},
			 $retval->{baktime}{day},
			 $retval->{baktime}{hour},
			 $retval->{baktime}{minute},
			 $retval->{baktime}{second},
			 $retval->{db_index},
			 $retval->{name},
			) = unpack("n C C C C n a4 a4 n N nCCCCCx nCCCCCx nCCCCCx n A*",
				$_->{'data'});

			warn "multiple entries from ReadDBList" if $num > 1;
		}
	}

	return undef unless exists $retval->{'name'};
	return $retval;
}

1;

__END__

=head1 SEE ALSO

ColdSync(3)

Palm::PDB(3)

F<ColdSync Conduits: Specification and Hacker's Guide>

=head1 AUTHOR

Andrew Arensburger E<lt>arensb@ooblick.comE<gt>
Alessandro Zummo E<lt>azummo@towertech.itE<gt>

=head1 BUGS

``SPC'' is a stupid name.

=cut
#'
