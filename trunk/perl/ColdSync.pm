# ColdSync.pm
# A module to simplify writing ColdSync conduits.
#
# $Id: ColdSync.pm,v 1.1 2000-01-25 10:27:23 arensb Exp $
package ColdSync;
BEGIN { $Exporter::Verbose = 1; }

# XXX - New API:
# For single-flavor conduits, use
#	StartConduit("fetch");
#
#	# Body of conduit goes here.
#
#	EndConduit;
#
# For multi-flavor conduits, use
#	ConduitMain(
#		fetch	=> \&MyFetchFunc,
#		dump	=> \&MyDumpFunc,
#	);

#use Exporter;
require Exporter;

@ISA = qw( Exporter );
@EXPORT = qw( $pdb ReadHeaders %headers ConduitMain );

BEGIN {
	$SIG{__WARN__} = \&Warn;
	$SIG{__DIE__} = \&Die;
}

# Warn
# Hook for warn(): when a conduit prints a warning, it should go to
# STDOUT, and be preceded by an error code.
sub Warn
{
	my $msg = shift;

	# XXX - Deal with multi-line warning messages.
	print STDOUT "301 " unless $msg =~ /^\d{3}[- ]/;
	print STDOUT $msg;

	return;
}

sub Die
{
	my $msg = shift;

	# XXX - Deal with multi-line warning messages.
	print STDOUT "501 " unless $msg =~ /^\d{3}[- ]/;
	print STDOUT $msg;

	return;
}

sub ReadHeaders
{
	while (<STDIN>)
	{
		chomp;
		return if $_ eq "";	# Empty line is end of headers

		# Get the header
		if (/^(\w+): (.*)/)
		{
			$headers{$1} = $2;
			next;
		}

		# This isn't a valid line
		die "401 Invalid input";
	}

	return;
}

sub ConduitMain
{
	my %flavors = @_;
	my $handler;			# Function that'll handle the
					# request

	# Check args: they should be of the form "conduit <flavor>".
	# Barf if the flavor is invalid.
	if ($ARGV[0] ne "conduit")
	{
		die "402 Missing conduit argument";
	}

	# Check flavor argument
	my $argflavor;

	# Make sure there's a flavor argument
	if (!defined($ARGV[1]))
	{
		die "402 Missing conduit flavor\n";
	}

	# Make sure the flavor given on the command line is valid
	if (lc($ARGV[1]) eq "fetch")
	{
		$argflavor = "fetch";
	} elsif (lc($ARGV[1]) eq "dump")
	{
		$argflavor = "dump";
# Not supported yet
#  	} elsif (lc($ARGV[1]) eq "sync")
#  	{
#  		$argflavor = "sync";
	} else {
		die "402 Invalid conduit flavor: $ARGV[1]\n";
	}

	# Make sure the flavor given on the command line is supported
	if (exists($flavors{$argflavor}))
	{
		$handler = $flavors{$argflavor};
	} elsif (exists($flavors{ucfirst($argflavor)}))
	{
		$handler = $flavors{ucfirst($argflavor)};
	} else {
		die "403 Unsupported flavor\n";
	}

	ReadHeaders;

	# XXX - Check for mandatory headers. These depend on the
	# conduit flavor.
	# Fetch:
	#	OutputDB
	# Dump:
	#	InputDB
	# Sync:
	#	InputDB

	# XXX - Actually, there may not be an InputDB, for Fetch
	# conduits.

	$pdb = new Palm::PDB;
	$pdb->Load($headers{"InputDB"}) or
		die "404 Can't read input database \"$headers{InputDB}\"";

	return if !defined($handler);

	# XXX - This breaks on symbolic references (i.e., when you
	# pass a function name, rather than a reference to a
	# function). Is this considered a Bad Thing?
	&{$handler};
}

1;
