# ColdSync::SPC::VFS.pm
#
# Module for dealing with SPC requests from ColdSync conduits.
#
#	Copyright (C) 2002, Alessandro Zummo.
#	You may distribute this file under the terms of the Artistic
#	License, as specified in the README file.
#
# $Id: VFS.pm,v 1.1 2002-04-24 13:28:10 azummo Exp $

# XXX - Write POD

use strict;
package ColdSync::SPC::VFS;

=head1 NAME

VFS - SPC functions for VFS support.

=head1 SYNOPSIS

    use ColdSync;
    use ColdSync::SPC::VFS;

=head1 DESCRIPTION

The SPC package includes SPC functions for VFS support.
You can use them to play with files on your memory card.


=cut

use ColdSync;
use ColdSync::SPC qw( :DEFAULT :dlp_vfs :dlp_args );
use Exporter;


@ColdSync::SPC::VFS::ISA	= qw( Exporter );
$ColdSync::SPC::VFS::VERSION	= sprintf "%d.%03d", '$Revision: 1.1 $ ' =~ m{(\d+)\.(\d+)};




# File Origin constants: (for the origins of relative offsets passed to 'seek' type routines)

use constant vfsOriginBeginning		=> 0;	# from the beginning (first data byte of file)
use constant vfsOriginCurrent		=> 1;	# from the current position
use constant vfsOriginEnd		=> 2;	# from the end of file 
						# (one position beyond last data byte,
						# only negative offsets are legal)

# openMode flags passed to VFSFileOpen
use constant vfsModeExclusive		=> 0x0001;			# Exclusive access
use constant vfsModeRead		=> 0x0002;			# Read access
use constant vfsModeWrite		=> 0x0004 | vfsModeExclusive;	# Write access, implies exclusive
use constant vfsModeCreate		=> 0x0008;			# Create the file if it doesn't exists.
use constant vfsModeTruncate		=> 0x0010;               	# Truncate file to 0 bytes after opening.
use constant vfsModeReadWrite		=> vfsModeWrite | vfsModeRead;	# Open for read/write access
use constant vfsModeLeaveOpen		=> 0x0020;			# Leave the file open even if when the


# File Attributes
use constant vfsFileAttrReadOnly	=> 0x00000001;
use constant vfsFileAttrHidden		=> 0x00000002;
use constant vfsFileAttrSystem		=> 0x00000004;
use constant vfsFileAttrVolumeLabel	=> 0x00000008;
use constant vfsFileAttrDirectory	=> 0x00000010;
use constant vfsFileAttrArchive		=> 0x00000020;
use constant vfsFileAttrLink		=> 0x00000040;
use constant vfsFileAttrAll             => 0x0000007F;


# Volume Attributes

use constant vfsVolumeAttrSlotBased	=> 0x00000001;	# Reserved
use constant vfsVolumeAttrReadOnly	=> 0x00000002;  # Volume is read only
use constant vfsVolumeAttrHidden	=> 0x00000004;	# Volume is not user visible


# For dlp_VFSFile(Get|Set)Date

use constant vfsFileDateCreated		=> 1;
use constant vfsFileDateModified	=> 2;
use constant vfsFileDateAccessed	=> 3;


@ColdSync::SPC::VFS::EXPORT = qw( 
	dlp_VFSVolumeEnumerate
	dlp_VFSVolumeInfo
	dlp_VFSVolumeGetLabel
	dlp_VFSVolumeSetLabel
	dlp_VFSVolumeGetSize
);


sub dlp_VFSVolumeEnumerate
{
	my $slotnum = shift;	# Slot number

	my ($err, @argv) = dlp_req(DLPCMD_VFSVolumeEnumerate);

	# Parse the return arguments.
	my $retval = {};

	foreach my $arg (@argv) {
		if ($arg->{id} == dlpFirstArgID)
		{
			@$retval
			{
				'numVolumes',
				'_data',

			} = unpack("n a*",$arg->{data});

			$retval->{'volumes'} = [];

			for (my $i = 0; $i < $retval->{'numVolumes'}; $i++)
			{
				my $ref;

				( $ref, $retval->{'_data'} ) = unpack('n a*', $retval->{'_data'});

				$retval->{'volumes'}[$i] = $ref;
			}

			delete $retval->{'_data'};
		}
	}

	return $retval;
}

sub dlp_VFSVolumeInfo
{
	my $refNum = shift;	# Volume refNum

	my ($err, @argv) = dlp_req(DLPCMD_VFSVolumeInfo,
				 {
					 id   => dlpFirstArgID,
					 data => pack("n", $refNum),
				 }
				 );

	# Parse the return arguments.
	my $retval = {};

	foreach my $arg (@argv) {
		if ($arg->{id} == dlpFirstArgID)
		{
			@$retval
			{
				'attributes',
				'fsType',
				'fsCreator',
				'mountClass',
				'slotLibRefNum',
				'slotRefNum',
				'mediaType',
				'reserved',

			} = unpack("N a4 a4 a4 n n a4 N",$arg->{data});
		}
	}

	return $retval;
}

sub dlp_VFSVolumeGetLabel
{
	my $refNum = shift;	# Volume refNum

	my ($err, @argv) = dlp_req(DLPCMD_VFSVolumeGetLabel,
				 {
					 id   => dlpFirstArgID,
					 data => pack("n", $refNum),
				 }
				 );

	# Parse the return arguments.
	my $retval = {};

	foreach my $arg (@argv) {
		if ($arg->{id} == dlpFirstArgID)
		{
			$retval->{'label'} = unpack("Z*", $arg->{data});
		}
	}

	return $retval;
}

sub dlp_VFSVolumeSetLabel
{
	my $refNum	= shift;	# Volume refNum
	my $label	= shift;

	my ($err, @argv) = dlp_req(DLPCMD_VFSVolumeSetLabel,
				 {
					 id   => dlpFirstArgID,
					 data => pack("n Z*", $refNum, $label),
				 }
				 );

	# No return arguments to parse
	return $err;
}

sub dlp_VFSVolumeGetSize
{
	my $refNum	= shift;	# Volume refNum

	my ($err, @argv) = dlp_req(DLPCMD_VFSVolumeGetLabel,
				 {
					 id   => dlpFirstArgID,
					 data => pack("n", $refNum),
				 }
				 );

	# Parse the return arguments.
	my $retval = {};

	foreach my $arg (@argv) {
		if ($arg->{id} == dlpFirstArgID)
		{
			@$retval
			{
				'volumeSizeUsed',
				'volumeSizeTotal',

			} = unpack("N N", $arg->{data});
		}
	}

	return $retval;
}


1;

__END__

=head1 SEE ALSO

ColdSync(3)

Palm::PDB(3)

ColdSync::SPC(3)

F<ColdSync Conduits: Specification and Hacker's Guide>

=head1 AUTHOR

Alessandro Zummo E<lt>azummo@towertech.itE<gt>

=cut
#'
