# ColdSync::SPC::VFS.pm
#
# Module for dealing with SPC requests from ColdSync conduits.
#
#	Copyright (C) 2002, Alessandro Zummo.
#	You may distribute this file under the terms of the Artistic
#	License, as specified in the README file.
#
# $Id: VFS.pm,v 1.4 2002-06-22 14:09:57 azummo Exp $

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
$ColdSync::SPC::VFS::VERSION	= sprintf "%d.%03d", '$Revision: 1.4 $ ' =~ m{(\d+)\.(\d+)};




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
									# the foreground task closes

# File Attributes
use constant vfsFileAttrReadOnly	=> 0x00000001;
use constant vfsFileAttrHidden		=> 0x00000002;
use constant vfsFileAttrSystem		=> 0x00000004;
use constant vfsFileAttrVolumeLabel	=> 0x00000008;
use constant vfsFileAttrDirectory	=> 0x00000010;
use constant vfsFileAttrArchive		=> 0x00000020;
use constant vfsFileAttrLink		=> 0x00000040;
use constant vfsFileAttrAll             => 0x0000007F;


# For dlp_VFSDirEntryEnumerate
use constant vfsIteratorStart		=> 0x00000000;
use constant vfsIteratorStop		=> 0xFFFFFFFF;


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
	dlp_VFSFileOpen
	dlp_VFSFileClose
	dlp_VFSFileRead
	dlp_VFSFileWrite
	dlp_VFSFileSize
	dlp_VFSFileResize
	dlp_VFSDirEntryEnumerate
);

%ColdSync::SPC::VFS::EXPORT_TAGS = (

	'vfs_opentags' => [ qw (

		vfsModeExclusive
		vfsModeRead
		vfsModeWrite
		vfsModeCreate
		vfsModeTruncate
		vfsModeReadWrite
		vfsModeLeaveOpen
	) ],

	'vfs_fileattrs' => [ qw (

		vfsFileAttrReadOnly
		vfsFileAttrHidden
		vfsFileAttrSystem
		vfsFileAttrVolumeLabel
		vfsFileAttrDirectory
		vfsFileAttrArchive
		vfsFileAttrLink
		vfsFileAttrAll
	) ],
);

Exporter::export_ok_tags('vfs_opentags','vfs_fileattrs');



sub dlp_VFSVolumeEnumerate
{
	my $slotnum = shift;	# Slot number

	my ($err, @argv) = dlp_req(DLPCMD_VFSVolumeEnumerate);

	return undef unless defined $err;

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

	return ($err, $retval);
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

	return undef unless defined $err;

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

	return ($err, $retval);
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

	return undef unless defined $err;

	# Parse the return arguments.
	my $retval = {};

	foreach my $arg (@argv) {
		if ($arg->{id} == dlpFirstArgID)
		{
			$retval->{'label'} = unpack("Z*", $arg->{data});
		}
	}

	return ($err, $retval, $retval->{'label'});
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

	return undef unless defined $err;

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

	return ($err, $retval);
}

sub dlp_VFSFileOpen
{
	my $volRefNum	= shift;	# Volume refNum
	my $openMode	= shift;	# Open mode
	my $path	= shift;	# File path

	my ($err, @argv) = dlp_req(DLPCMD_VFSFileOpen,
				 {
					 id   => dlpFirstArgID,
					 data => pack("n n Z*", $volRefNum, $openMode, $path),
				 }
				 );

	return undef unless defined $err;

	# Parse the return arguments.
	my $retval = {};

	foreach my $arg (@argv) {
		if ($arg->{id} == dlpFirstArgID)
		{
			$retval->{'fileRef'} = unpack("N",$arg->{data});
		}
	}


	return ($err, $retval, $retval->{'fileRef'});
}

sub dlp_VFSFileClose
{
	my $fileRef	= shift;	# file refNum

	my ($err, @argv) = dlp_req(DLPCMD_VFSFileClose,
				 {
					 id   => dlpFirstArgID,
					 data => pack("N", $fileRef),
				 }
				 );

	# No return arguments to parse
	return $err;
}

sub dlp_VFSFileRead
{
	my $fileRef	= shift;	# fileRef
	my $numBytes	= shift;	# bytes to read

	my ($err, @argv) = dlp_req(DLPCMD_VFSFileRead,
				 {
					 id   => dlpFirstArgID,
					 data => pack("N N", $fileRef, $numBytes),
				 }
				 );

	return undef unless defined $err;

	# Parse the return arguments.
	my $retval = {};

	foreach my $arg (@argv) {
		if ($arg->{id} == dlpFirstArgID)
		{
			@$retval{
				'numBytes',
			} = unpack("N",$arg->{data});
		}
	}

	# Read raw data
	$retval->{'data'} = spc_recv( $retval->{'numBytes'} );

	return ($err, $retval);
}

sub dlp_VFSFileWrite
{
	my $fileRef	= shift;	# fileRef
	my $data	= shift;	#

	my ($err, @argv) = dlp_req(DLPCMD_VFSFileWrite,
				 {
					 id   => dlpFirstArgID,
					 data => pack("N N", $fileRef, length( $data )),
				 }
				 );

	return undef unless defined $err;

	print STDERR "File write failed: $err\n" unless $err eq 0;

	# Parse the return arguments.
	# XXX - Probably no args
	my $retval = {};

	foreach my $arg (@argv) {
		if ($arg->{id} == dlpFirstArgID)
		{
			@$retval{
				'unknown',
			} = unpack("a*",$arg->{data});
		}
	}

	# Write raw data
	spc_send( $data );

	return ($err, $retval);
}

sub dlp_VFSFileSize
{
	my $fileRef	= shift;	# fileRef

	my ($err, @argv) = dlp_req(DLPCMD_VFSFileSize,
				 {
					 id   => dlpFirstArgID,
					 data => pack("N", $fileRef),
				 }
				 );

	return undef unless defined $err;

	# Parse the return arguments.
	my $retval = {};

	foreach my $arg (@argv) {
		if ($arg->{id} == dlpFirstArgID)
		{
			$retval->{'fileSize'} = unpack("N",$arg->{data});
		}
	}

	return ($err, $retval, $retval->{'fileSize'});
}

sub dlp_VFSFileResize
{
	my $fileRef	= shift;	# fileRef
	my $newSize	= shift;

	my ($err, @argv) = dlp_req(DLPCMD_VFSFileResize,
				 {
					 id   => dlpFirstArgID,
					 data => pack("N N", $fileRef, $newSize),
				 }
				 );

	# No return arguments to parse
	return $err;
}


sub dlp_VFSDirEntryEnumerate
{
	my $dirRefNum		= shift;	# directory refnum, as
						# obtained from dlp_VFSFileOpen
	my $dirEntryIterator	= shift;	# 
	my $bufferSize		= shift;	# 

	my ($err, @argv) = dlp_req(DLPCMD_VFSDirEntryEnumerate,
				 {
					 id   => dlpFirstArgID,
					 data => pack("N N N", $dirRefNum, $dirEntryIterator, $bufferSize),
				 }
				 );

	return undef unless defined $err;

	# Parse the return arguments.
	my $retval = {};

	foreach my $arg (@argv) {
		if ($arg->{id} == dlpFirstArgID)
		{
			my $data;

			# Fetch header
			
			( @$retval{
				'dirEntryIterator',
				'numEntries'
			}, $data ) = unpack("N N a*",$arg->{data});


			# Fetch directory entries

			$retval->{'entries'} = [];

			for ( my $i = 0; $i < $retval->{'numEntries'}; $i++ )
			{
				my ( $attrs, $name );

				( $attrs, $data ) = unpack( "N a*", $data );

				# Split out the first string.
				( $name, $data ) = split( /\0/, $data, 2 );

				# Discard the pad byte if the string ( +
				# null terminator) has an odd length. 

				$data = substr($data, 1)
					if ((length($name) % 2) == 0);

				
				my $attr = {};

				my $entry = {

					'attributes'	=> $attrs,
					'attribute'	=> $attr,
					'name'		=> $name
				};

				# Parse attributes

				$attr->{'ReadOnly'}	= $attrs & vfsFileAttrReadOnly		? 1 : 0;
				$attr->{'Hidden'}	= $attrs & vfsFileAttrHidden		? 1 : 0;
				$attr->{'System'}	= $attrs & vfsFileAttrSystem		? 1 : 0;
				$attr->{'VolumeLabel'}	= $attrs & vfsFileAttrVolumeLabel	? 1 : 0;
				$attr->{'Directory'}	= $attrs & vfsFileAttrDirectory 	? 1 : 0;
				$attr->{'Archive'}	= $attrs & vfsFileAttrArchive		? 1 : 0;
				$attr->{'Link'}		= $attrs & vfsFileAttrLink		? 1 : 0;

				push( @{$retval->{'entries'}}, $entry);
			}			
		}
	}

	return ($err, $retval);
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
