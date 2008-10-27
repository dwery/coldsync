# ColdSync::SPC::ExpSlot.pm
#
# Module for dealing with SPC requests from ColdSync conduits.
#
#	Copyright (C) 2002, Alessandro Zummo.
#	You may distribute this file under the terms of the Artistic
#	License, as specified in the README file.
#
# $Id$

# XXX - Write POD

use strict;
package ColdSync::SPC::ExpSlot;

=head1 NAME

ExpSlot - SPC functions for ExpSlot support.

=head1 SYNOPSIS

    use ColdSync;
    use ColdSync::SPC::ExpSlot;

=head1 DESCRIPTION

The SPC package includes SPC functions for ExpSlot support.
You can use them to play with files on your memory card.


=cut

use 5.008_000;

use ColdSync;
use ColdSync::SPC qw( :DEFAULT :dlp_expslot :dlp_args );
use Exporter;


@ColdSync::SPC::ExpSlot::ISA	 = qw( Exporter );
$ColdSync::SPC::ExpSlot::VERSION = do { my @r = (q$Revision: 1.6 $ =~ /\d+/g); sprintf "%d."."%02d" x $#r, @r };


@ColdSync::SPC::ExpSlot::EXPORT = qw( 
	dlp_ExpSlotEnumerate
	dlp_ExpSlotMediaType
	dlp_ExpCardPresent
	dlp_ExpCardInfo
);


sub dlp_ExpSlotMediaType
{
	my $slotnum = shift;	# Slot number

	my ($err, @argv) = dlp_req(DLPCMD_ExpSlotMediaType,
				 {
					 id   => dlpFirstArgID,
					 data => pack("n", $slotnum),
				 }
				 );

	return undef unless defined $err;

	# Parse the return arguments.
	my $retval = {};

	foreach my $arg (@argv) {
		if ($arg->{id} == dlpFirstArgID)
		{
			$retval->{'slot'}	= $slotnum;
			$retval->{'mediatype'}	= unpack("a4",$arg->{data});
		}
	}

	return ($err, $retval);
}

sub dlp_ExpCardPresent
{
	my $slotnum = shift;	# Slot number

	my ($err, @argv) = dlp_req(DLPCMD_ExpCardPresent,
				 {
					 id   => dlpFirstArgID,
					 data => pack("n", $slotnum),
				 }
				 );

	return undef unless defined $err;

	return $err;
}

sub dlp_ExpSlotEnumerate
{
	my $slotnum = shift;	# Slot number

	my ($err, @argv) = dlp_req(DLPCMD_ExpSlotEnumerate);

	return undef unless defined $err;

	# Parse the return arguments.
	my $retval = {};

	foreach my $arg (@argv) {
		if ($arg->{id} == dlpFirstArgID)
		{
			@$retval
			{
				'numSlots',
				'_data',
			} = unpack("n a*",$arg->{data});

			$retval->{'slots'} = [];

			for (my $i = 0; $i < $retval->{'numSlots'}; $i++)
			{
				my $ref;

				($ref, $retval->{'_data'}) = unpack('n a*', $retval->{'_data'});

				$retval->{'slots'}[$i] = $ref;
			}

			delete $retval->{'_data'};
		}
	}

	return ($err, $retval);
}

sub dlp_ExpCardInfo
{
	my $slotnum = shift;	# Slot number

	my ($err, @argv) = dlp_req(DLPCMD_ExpCardInfo,
				 {
					 id   => dlpFirstArgID,
					 data => pack("n", $slotnum),
				 }
				 );

	return undef unless defined $err;

	# Parse the return arguments.
	my $retval = {};

	foreach my $arg (@argv) {
		if ($arg->{id} == dlpFirstArgID)
		{
			my $data;

			(
				$retval->{'capabilities'},
				$retval->{'numstrings'},
				$data
			) = unpack("N n a*", $arg->{data});

			# Workaround for buggy 5.x devices: returned values
			# are in little-endian byte sex ;)
			# (confirmed on TT and Zire71).

			if ($retval->{'numstrings'} eq 1024)
			{
				(
					$retval->{'capabilities'},
					$retval->{'numstrings'},
					$data
				) = unpack("V v xx a*", $arg->{data});
			}

			# Retrieve the strings
			my @s = unpack("(Z*)$retval->{'numstrings'}", $data);

			# Store them
			$retval->{'strings'} = \@s;

			# Expand capability flags

			use constant expCapabilityHasStorage	=> 0x00000001; # card supports reading (and maybe writing)
			use constant expCapabilityReadOnly	=> 0x00000002; # card is read only
			use constant expCapabilitySerial	=> 0x00000004; # card supports dumb serial interface

			$retval->{'capability'}{'HasStorage'}	= $retval->{'capabilities'} & expCapabilityHasStorage ? 1 : 0;
			$retval->{'capability'}{'ReadOnly'}	= $retval->{'capabilities'} & expCapabilityReadOnly ? 1 : 0;
			$retval->{'capability'}{'Serial'}	= $retval->{'capabilities'} & expCapabilitySerial ? 1 : 0;
		}
	}

	return ($err, $retval);
}

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
