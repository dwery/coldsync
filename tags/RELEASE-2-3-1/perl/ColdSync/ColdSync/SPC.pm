# ColdSync::SPC.pm
#
# Module for dealing with SPC requests from ColdSync conduits.
#
#	Copyright (C) 2000, Andrew Arensburger.
#	You may distribute this file under the terms of the Artistic
#	License, as specified in the README file.
#
# $Id: SPC.pm,v 1.8 2002-03-08 02:39:23 arensb Exp $

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

use vars qw( $VERSION @ISA *SPC @EXPORT );
$VERSION = sprintf "%d.%03d", '$Revision: 1.8 $ ' =~ m{(\d+)\.(\d+)};

@ISA = qw( Exporter );
@EXPORT = qw( spc_req *SPC
	dlp_req
	spc_get_dbinfo
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
	dlp_ReadRecordByIndex
	dlp_ReadRecordById
	dlp_AddSyncLogEntry
	dlp_DeleteRecord
	dlp_DeleteAllRecords
	dlp_WriteRecord
);

# Various useful constants
use constant SPCOP_NOP		=> 0;
use constant SPCOP_DBINFO	=> 1;
use constant SPCOP_DLPC		=> 2;
use constant SPCOP_DLPR		=> 3;

use constant SPCERR_OK		=> 0;
use constant SPCERR_BADOP	=> 1;
use constant SPCERR_NOMEM	=> 2;

use constant dlpRespErrNone	=> 0;

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

	# Read the reply data
	if ($len > 0)
	{
		read SPC, $buf, $len;
	}

	return ($status, $buf);
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
	my $code;
	my $argc;
	my $errno;
	my @argv;

	($code, $argc, $errno) = unpack("C C n", $data);
			# $code should be $cmd | 0x80, but I'm not checking.
	$data = substr($data, 4);	# Just the arguments

	# Do minimal unpacking of return args into { id => X, data => Y }
	@argv = unpack_dlp_args($argc, $data);

	return ($errno, @argv);
}

# pack_dlp_args
# Takes a set of arguments of the form
#	{ id => 123, data => "abc" }
# Packs each one as a DLP argument, concatenates them, and returns the
# result.
sub pack_dlp_args
{
	my $arg;
	my $retval = "";

	foreach $arg (@_)
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
	my $i;

	# Set the length of @retval
	$#retval = $argc-1;

	# Unpack each argument in turn
	for ($i = 0; $i < $argc; $i++)
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
sub dlp_ReadSysInfo
{
	my $errno;
	my @argv;
	my $retval;

	# Send the request
	($errno, @argv) = dlp_req(DLPCMD_ReadSysInfo);

	# Unpack the arguments further
	my $arg;

	$retval = {};
	foreach $arg (@argv)
	{
		if ($arg->{id} == 0x20)
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

		} elsif ($arg->{id} == 0x21)
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
	my $err;
	my @args;

	($err, @args) = dlp_req(DLPCMD_OpenDB,
			{
				id	=> 0x20,
				data	=> pack("C C a*x",
					$cardNo, $mode, $dbname),
			});

	return undef if !defined($err);

	# Parse the return arguments
	my $retval;

	for (@args)
	{
		if ($_->{id} == 0x20)
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
	my $err;
	my @args;

	($err, @args) = dlp_req(DLPCMD_CloseDB,
			{
				id	=> 0x20,
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
	my $err;
	my @args;

	($err, @args) = dlp_req(DLPCMD_DeleteDB,
			{
				id	=> 0x20,
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

	my ($err, @args) = dlp_req(DLPCMD_ReadOpenDBInfo,
			{
				id	=> 0x20,
				data	=> pack("C", $dbh),
			});

	return undef unless defined $err;

	# Parse the return arguments
	my $retval = {};
	

	# Default values
	$retval->{'numrecords'} = undef;

	for (@args)
	{
		if ($_->{id} == 0x20)
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

I don't think this command does anything right now.

=cut
#'
sub dlp_CleanUpDatabase
{
	my $dbh = shift;	# Database handle

	my ($err, @args) = dlp_req(DLPCMD_CleanUpDatabase,
			{
				id	=> 0x20,
				data	=> pack("C", $dbh),
			});

	return undef unless defined $err;

	return $err;
}

=head2 dlp_AddSyncLogEntry

	dlp_AddSyncLogEntry($dbh);

Adds an entry in the sync log for the database associated with the
database handle C<$dbh>.

=cut
#'
sub dlp_AddSyncLogEntry
{
	my $text = shift;	# Text to add to the log file

	my ($err, @args) = dlp_req(DLPCMD_AddSyncLogEntry,
			{
				id	=> 0x20,
				data	=> pack("Z*", $text),
			});

	return undef unless defined $err;

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
	my ($err, @args) = dlp_req(DLPCMD_ReadUserInfo);

	return undef unless defined $err;

	# Parse the return arguments
	my $retval = {};
	

	# Some defaults

	$retval->{"clastsyncdate"} = undef;
	$retval->{"csuccsyncdate"} = undef;
	

	for (@args)
	{
		if ($_->{id} == 0x20)
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
			 # cares, ve can measure it again.
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

			# XXX - ColdSync has some problems when
			# syncing with a Palm with a password, so this
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
	my $dbh = shift;	# Database handle
	my $recordid = shift;	# Id of the record to delete
	my $flags = shift;	# Flags. Set to 0 for normal use.


	$flags = 0 unless defined $flags;

	my ($err, @args) = dlp_req(DLPCMD_DeleteRecord,
			{
				id	=> 0x20,
				data	=> pack("C C N",
						$dbh, $flags, $recordid),
			});

	return undef unless defined $err;

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
	my $dbh = shift;	# Database handle
	my $id = shift;		# Record id
	my $offset = shift;	# Offset into the record
	my $numbytes = shift;	# Number of bytes to read starting at 
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
	my $dbh = shift;	# Database handle
	my $index = shift;	# Record index
	my $offset = shift;	# Offset into the record
	my $numbytes = shift;	# Number of bytes to read starting at
				# the offset (-1 = "to the end")

	return dlp_ReadRecord(0, $dbh, $index, $offset, $numbytes);
}



sub _dlp_ReadRecord
{
	my $readbyid = shift;	# Read record (1: by id, 0: by index)
	my $dbh = shift;	# Database handle
	my $idindex = shift;	# Record index (or record id)
	my $offset = shift;	# Offset into the record

	my $numbytes = shift;	# Number of bytes to read starting at
				# the offset (-1 = "to the end")

	$offset = 0 unless defined $offset;
	$numbytes = -1 unless defined $numbytes;

	my ($err, @args) = dlp_req(DLPCMD_ReadRecord,
			{
				id	=> $readbyid ? 0x20 : 0x21,
				data	=> pack("C x n n n",
						$dbh, $idindex, $offset,
						$numbytes),
			});

	return undef unless defined $err;
	return undef unless $err eq dlpRespErrNone;

	# XXX - I think there should be a way for the caller
	# to retrieve the original error code. Any idea?
	# (Should be implemented coherently in every dlp
	# function) - az.
	my $retval = {};
	

	for (@args)
	{
		if ($_->{id} == 0x20)
		{
			($retval->{'id'},
			 $retval->{'index'},
			 $retval->{'size'},
			 $retval->{'attrs'},
			 $retval->{'category'},
			 $retval->{'data'},
			) = unpack("N n n C C a*", $_->{data});

			# XXX - I used the names in DLCommon.h for the
			# attributes. This leads to an incompatibility
			# if someone wants to do something like this:
			#
			# $r = dlp_ReadRecordByIndex($dbh, $index);
			#
			# my $record = $pdb->ParseRecord(%{$r}});
			# $pdb->append_Record($record);
			#
			# What should we do ?

			$retval->{'attributes'}{'deleted'}	=
				$retval->{'attrs'} & 0x80 ? 1 : 0;
			$retval->{'attributes'}{'dirty'}	=
				$retval->{'attrs'} & 0x40 ? 1 : 0;
			$retval->{'attributes'}{'busy'}		=
				$retval->{'attrs'} & 0x20 ? 1 : 0;
			$retval->{'attributes'}{'secret'}	=
				$retval->{'attrs'} & 0x10 ? 1 : 0;
			$retval->{'attributes'}{'archived'}	=
				$retval->{'attrs'} & 0x08 ? 1 : 0;

			my $recsize = length($retval->{'data'});

			# XXX - This is untested. 

#			die("dlp_ReadRecord: Bad record size. Expected: $retval->{'size'}, G ot: $recsize .\n")
#				unless $retval->{'size'} == $recsize;
		}
	}

	return $retval;
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

	my ($err, @args) = dlp_req(DLPCMD_WriteRecord,
			{
				id	=> 0x20,
				data	=> pack("C C N C C a*",
						$dbh, $flags, $id, $attrs,
						$category, $data),
			});

	return undef unless defined $err;
	return undef unless $err eq dlpRespErrNone;

	my $retval = {};

	for (@args)
	{
		if ($_->{id} == 0x20)
		{
			$retval->{'newid'} = unpack("N", $_->{data});
		}
	}

	return $retval;
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
	my $dbh = shift;	# Database handle
	my $offset = shift;	# Offset into AppInfo block. Optional,
				# defaults to 0
	my $len = shift;	# # bytes to read. Optional, defaults
				# to "to end of AppInfo block".

	$offset = 0 if !defined($offset);
	$len = -1 if !defined($len);

	my $err;
	my @args;

	($err, @args) = dlp_req(DLPCMD_ReadAppBlock,
			{
				id	=> 0x20,
				data	=> pack("Cx n n",
						$dbh, $offset, $len),
			});

	# Parse the return arguments
	my $retval;

	for (@args)
	{
		if ($_->{id} == 0x20)
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
	my $dbh = shift;	# Database handle
	my $data = shift;	# AppInfo block to upload
	my $err;
	my @args;

	($err, @args) = dlp_req(DLPCMD_WriteAppBlock,
			{
				id	=> 0x20,
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
	my $errno;
	my @argv;
	my $retval;

	($errno, @argv) = dlp_req(DLPCMD_GetSysDateTime);

	$retval = {};
	foreach my $arg (@argv) {
		if ($arg->{id} == 0x20) {
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

	my $year = shift;
	my $mon = shift;
	my $day = shift;
	my $hour = shift;
	my $min = shift;
	my $sec = shift;
	my $err;
	my @argv;

	if (ref($year) eq 'HASH')
	{
		my %newtime = %$year;

		(
		 $year, $mon, $day,
		 $hour, $min, $sec
		) = @newtime{'year', 'month', 'day', 'hour', 'minute',
				'second'};
	}

	($err, @argv) = dlp_req(DLPCMD_SetSysDateTime,
				 {
					 id   => 0x20,
					 data => pack("nC5x",
						      $year, $mon, $day,
						      $hour, $min, $sec)
				 }
				 );

	# No return arguments to parse
	return $err;
}

1;

__END__

=head1 SEE ALSO

ColdSync(3)

Palm::PDB(3)

F<ColdSync Conduits: Specification and Hacker's Guide>

=head1 AUTHOR

Andrew Arensburger E<lt>arensb@ooblick.comE<gt>

=head1 BUGS

``SPC'' is a stupid name.

=cut
#'