# ColdSync::SPC.pm
#
# Module for dealing with SPC requests from ColdSync conduits.
#
#	Copyright (C) 2000, Andrew Arensburger.
#	You may distribute this file under the terms of the Artistic
#	License, as specified in the README file.
#
# $Id: SPC.pm,v 1.4 2000-09-04 07:03:50 arensb Exp $

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

SPC - Allows ColdSync conduits to communicate with the Palm.

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
($VERSION) = '$Revision: 1.4 $ ' =~ /\$Revision:\s+([^\s]+)/;
@ISA = qw( Exporter );
@EXPORT = qw( spc_req *SPC
	dlp_req
	spc_get_dbinfo
	dlp_ReadSysInfo
	dlp_OpenDB
	dlp_CloseDB
	dlp_ReadAppBlock
	dlp_WriteAppBlock
);

# Various useful constants
use constant SPCOP_NOP		=> 0;
use constant SPCOP_DBINFO	=> 1;
use constant SPCOP_DLPC		=> 2;
use constant SPCOP_DLPR		=> 3;

use constant SPCERR_OK		=> 0;
use constant SPCERR_BADOP	=> 1;
use constant SPCERR_NOMEM	=> 2;

use constant DLPCMD_ReadSysInfo		=> 0x12;
use constant DLPCMD_OpenDB		=> 0x17;
use constant DLPCMD_CloseDB		=> 0x19;
use constant DLPCMD_ReadAppBlock	=> 0x1b;
use constant DLPCMD_WriteAppBlock	=> 0x1c;

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

    ($err, @argv) = &dlp_req($reqno, @args)

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
# ($err, @argv) = &dlp_req($reqno, @args)
# where
# $err is the return status,
# @argv are the (unparsed) returned arguments
# $reqno is the DLPCMD_* request number
# @args are the (unparsed) parameters
sub dlp_req
{
	my $cmd = shift;	# DLP command
	# All other arguments are DLP arguments

	my $dlp_req;		# DLP request data

	# Construct DLP header
	$dlp_req = pack("C C", $cmd, $#_+1);

	# Pack DLP arguments
	$dlp_req .= &pack_dlp_args;

	# Send it as an SPCOP_DLPC request
	my $status;
	my $data;
	($status, $data) = &spc_req(SPCOP_DLPC, $dlp_req);

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
	@argv = &unpack_dlp_args($argc, $data);

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
	my $retval;

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

	$dbinfo = &spc_get_dbinfo();

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
	($status, $data) = &spc_req(SPCOP_DBINFO, undef);
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
	# XXX - Consider mktime()ing the *time fields to get the times
	# in standard Unix format.

	return $retval;
}

=head2 dlp_ReadSysInfo

	$sysinfo = &dlp_ReadSysInfo();

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
	($errno, @argv) = &dlp_req(DLPCMD_ReadSysInfo);

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

	$dbh = &dlp_OpenDB($dbname, $mode);

Opens a database on the Palm. C<$dbname> is a string indicating the
name of the database; the name of the current database can be gotten
from C<&spc_get_dbinfo>.

C<$mode> indicates how to open the database. It is the bitwise-or of
any of the following values:

	0x80		Open for reading
	0x40		Open for writing
	0x20		Exclusive access
	0x10		Show secret records

If successful, C<&dlp_OpenDB> returns a database handle: a small
integer that refers to the database that was just opened. The database
handle will be passed to various other functions that operate on the
database.

If unsuccessful, C<&dlp_OpenDB> returns C<undef>.

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

	($err, @args) = &dlp_req(DLPCMD_OpenDB,
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

	&dlp_CloseDB($dbh);

Closes the database associated with the database handle C<$dbh> (see
L</dlp_OpenDB>).

=cut
#'
sub dlp_CloseDB
{
	my $dbh = shift;	# Database handle
	my $err;
	my @args;

	($err, @args) = &dlp_req(DLPCMD_CloseDB,
			{
				id	=> 0x20,
				data	=> pack("C", $dbh),
			});
	return $err;
}

=head2 dlp_ReadAppBlock

	$appinfo = &dlp_ReadAppBlock($dbh [, $offset, $len]);

Reads the AppInfo block of the database associated with database
handle C<$dbh> (see L</dlp_OpenDB>).

C<$offset> is an integer specifying an offset from the beginning of
the AppInfo block at which to start reading. C<$len> is the length of
the data to return.

If omitted, C<$offset> defaults to 0 (read from the beginning) and
C<$len> defaults to -1 (read to the end).

The value returned by C<&dlp_ReadAppBlock> is a string of binary data.
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

	($err, @args) = &dlp_req(DLPCMD_ReadAppBlock,
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

	$err = &dlp_WriteAppBlock($dbh, $appinfo);

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

	($err, @args) = &dlp_req(DLPCMD_WriteAppBlock,
			{
				id	=> 0x20,
				data	=> pack("Cx n a*",
						$dbh, length($data),
						$data),
			});
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
