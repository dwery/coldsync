# ColdSync.pm
# A module to simplify writing ColdSync conduits.
#
#	Copyright (C) 1999, 2000, Andrew Arensburger.
#	You may distribute this file under the terms of the Artistic
#	License, as specified in the README file.
#
# $Id: ColdSync.pm,v 1.10 2000-09-03 05:04:41 arensb Exp $
package ColdSync;
($VERSION) = '$Revision: 1.10 $ ' =~ /\$Revision:\s+([^\s]+)/;

=head1 NAME

ColdSync - Convenience module for writing ColdSync conduits.

=head1 SYNOPSIS

Single-flavor conduits:

    use ColdSync;

    StartConduit(<flavor>);

    # Body of conduit

    EndConduit;

Multi-flavor conduits:

    use ColdSync;

    ConduitMain(
        "dump"	=> \&doDump,
        "fetch"	=> \&doFetch,
        "sync"	=> \&doSync,
        );

    sub doDump...
    sub doFetch...

=head1 DESCRIPTION

The ColdSync module provides helper functions for writing ColdSync
conduits. This manual page does not describe conduits or how they
work; for this, the reader is referred to I<ColdSync Conduits:
Specification and Hacker's Guide>.

The functions in this module support both Fetch, Dump, and Sync conduits,
and perform a certain amount of sanity-checking on the conduit's input.

=cut
#'

use Exporter;
use Palm::PDB;

@ISA = qw( Exporter );
@EXPORT = qw( $PDB %HEADERS @HEADERS %PREFERENCES
		ConduitMain StartConduit EndConduit );

=head1 VARIABLES

The following variables are exported to the caller by default:

=over 2

=item $PDB

Holds a reference to a Palm::PDB object containing the database being
synchronized. If the conduit was passed an C<InputDB> header argument,
it will be read into $PDB. When the conduit terminates, if it is
expected to write a Palm database, it will write $PDB.

=item %HEADERS

Holds the headers passed to the conduit on STDIN. Duplicate headers
are not supported. If a conduit is passed multiple headers with the
same label, only the last one is recorded in %HEADERS.

=item @HEADERS

Holds the list of header lines passed in on STDIN, in the order in
which they were seen. This can be useful if your conduit allows
multiple headers, or if the order in which the headers were received
matters.

=item %PREFERENCES

Holds the preferences passed on STDIN. The keys of this hash are the
creators of the preference items. Their values, in turn, are references to
hashes whose key are the IDs, and whose values are the raw preference item.

Thus C<$PREFERENCES{"mail"}{6}> contains the preference item whose creator
is C<mail> and whose ID is 6.

=cut
#'

$FLAVOR = undef;		# Flavor with which this conduit was invoked

# Lists the headers that are required for each flavor of conduit
%MANDATORY_HEADERS = (
	"fetch"	=> [ qw( Daemon Version OutputDB ) ],
	"dump"	=> [ qw( Daemon Version InputDB ) ],
	"sync"	=> [ qw( Daemon Version InputDB OutputDB ) ],
);

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

# Die
# Hook for die(): when a conduit dies, the error message should go to
# STDOUT, and be preceded by an error code.
sub Die
{
	my $msg = shift;

	# XXX - Should run exit hooks, if applicable

	# XXX - Deal with multi-line warning messages.
	print STDOUT "501 " unless $msg =~ /^\d{3}[- ]/;
	print STDOUT $msg;

	return;
}

# DumpConfig
# Write configuration information to stdout, in a format suitable for
# inclusion in .coldsyncrc.
sub DumpConfig
{
	my @flavors = @_;
	my $flavor;
	my $creator;
	my $type;
	my @typestrings = ();
	my $typestring;

	foreach $creator (keys %Palm::PDB::PDBHandlers)
	{
		foreach $type (keys %{$Palm::PDB::PDBHandlers{$creator}})
		{
			# Handle wildcards
			$creator = "*" if $creator eq "";
			$type = "*" if $type eq "";

			push @typestrings, "$creator/$type";
		}
	}

	foreach $flavor (@flavors)
	{
		foreach $typestring (@typestrings)
		{
			# XXX - The $0 may be incorrect
			print <<EOT;
conduit $flavor {
	path "$0";
	type $typestring;
EOT
			# If %HEADERS contains any default values, list
			# them.

			# XXX - Doesn't deal properly with some headers: if
			# the header has leading or trailing whitespace, it
			# should be quoted. This requires a rewrite of the
			# corresponding lex/yacc code to accept quotes,
			# though.

			if (%HEADERS ne ())
			{
				my $key;
				my $value;

				print "    arguments:\n";
				while (($key, $value) = each %HEADERS)
				{
					print "#\t$key:\t$value\n";
				}
			}

			print "}\n";
		}
	}

	# XXX - Now do the same thing for resource databases, once
	# those become supported.
}

# ParseArgs
# parse command-line arguments
sub ParseArgs
{
	my @flavors = @_;

	if ($ARGV[0] ne "conduit")
	{
		# This conduit was not invoked as a conduit, but
		# rather as a standalone program.
		if ($ARGV[0] eq "-config")
		{
			&DumpConfig(@_);
			exit 0;
		}

		# At this point, the $SIG{__DIE__} handler hasn't
		# been installed yet.
		print STDOUT "402 Missing conduit argument\n";
		exit 1;
	}

	# This program isn't being run standalone
	$SIG{__WARN__} = \&Warn;
	$SIG{__DIE__} = \&Die;

	# Check flavor argument

	# Make sure there's a flavor argument
	if (!defined($ARGV[1]))
	{
		print STDOUT "402 Missing conduit flavor\n";
		exit 1;
	}

	# Make sure the flavor given on the command line is valid
	if (lc($ARGV[1]) eq "fetch")
	{
		$FLAVOR = "fetch";
	} elsif (lc($ARGV[1]) eq "dump")
	{
		$FLAVOR = "dump";
	} elsif (lc($ARGV[1]) eq "sync")
	{
		$FLAVOR = "sync";
	} else {
		print STDOUT "402 Invalid conduit flavor: $ARGV[1]\n";
		exit 1;
	}
}

# ReadHeaders
# Read the conduit headers from stdin.
sub ReadHeaders
{
	my @preflist;		# List of preferences to read from STDIN:
				# Each element is an anonymous array:
				#	[ creator, ID, length ]
	my $len;
	my $i;

	while (<STDIN>)
	{
		chomp;
		last if $_ eq "";	# Empty line is end of headers

		push @HEADERS, $_;

		# Get the preference
		if(m{^Preference: (\w\w\w\w)/(\d+)/(\d+)})
		{
			# Put the creator, ID, and length in an anonymous
			# array, and save it for later.
			push @preflist, [$1, $2, $3];
			next;
		}

		# Get the header
		if (/^([-\w]+): (.*)/)
		{
			$HEADERS{$1} = $2;
			next;
		}

		# This isn't a valid line
		die "401 Invalid input: [$_]";
	}

	# Now read all the raw preference items from STDIN
	# They are being read in the same order as the items were specified
	# XXX - What happens if somehow less data is written to stdout?
	# If we believe perlfunc, read is like fread so will simply read
	# as much as possible, but less if not enough is provided, so that
	# the conduit won't hang here, waiting for input. Not tested yet,
	# however...
	my $creator;		# Preference creator
	my $pref_id;		# Preference ID
	my $pref_len;		# Preference length

	while (($creator, $pref_id, $pref_len) = @{shift @preflist})
	{
		my $data;

		read STDIN, $data, $pref_len;
		$PREFERENCES{$creator}{$id} = $data;
	}

	# Make sure all of the mandatory headers are there.
	my $required;

	foreach $required (@{$MANDATORY_HEADERS{$FLAVOR}})
	{
		if (!defined($HEADERS{$required}))
		{
			die "404 Missing $required header\n";
		}
	}

	return;
}

=head1 FUNCTIONS

=over 2

=item StartConduit(I<flavor>)

Initializes a single-flavor conduit. Its argument is a string specifying
the flavor of the conduit, either C<"fetch">, C<"dump">, or C<"sync">.

StartConduit() reads and checks the conduit's command line arguments,
reads the headers given on STDIN, and makes sure that all of the
mandatory headers for the flavor in question are present. If an
C<InputDB> header was given, loads the named file into $PDB.

If the program is run not as a conduit but as a standalone program,
StartConduit() supports the C<-config> option: when this option is
given, the program prints to STDOUT a sample configuration entry that
may be appended to F<.coldsyncrc>.

=cut
#'

# StartConduit
sub StartConduit
{
	my $flavor = shift;

	&ParseArgs($flavor);

	if ($FLAVOR ne $flavor)
	{
		die "403 Unsupported flavor\n";
	}

	ReadHeaders;

	# Read the input database, if one was specified.
	$PDB = new Palm::PDB;
	if (defined($HEADERS{InputDB}))
	{
		$PDB->Load($HEADERS{InputDB}) or
			die "404 Can't read input database \"$HEADERS{InputDB}\"";
	}

	# Open the SPC pipe, if requested
	# XXX - This is ugly. A better solution would be to add arrays of
	# hooks: @before_conduit_hooks, @before_sync_hooks,
	# @after_conduit_hooks, @after_sync_hooks, etc. Each array holds
	# code refrences. These are executed in order at the appropriate
	# times (@after_*_hooks are executed in reverse order: that way you
	# use 'push' to add either type of hook. And since @after_*_hooks
	# would tend to undo what was done by @before_*_hooks, things get
	# undone in the proper order.
	if (exists $ColdSync::SPC::{"VERSION"} and
	    defined $HEADERS{"SPCPipe"})
	{
		&ColdSync::SPC::spc_init;
	}
}

=item EndConduit()

Cleans up after a single-flavor conduit. For Fetch and Sync conduits,
writes $PDB to the file given by $HEADERS{OutputDB}. If everything went
well, exits with status 0.

Dump conduits are not expected to write a Palm database, so
EndConduit() does not do so. Any Dump conduit that wishes to write a
database must do so explicitly.

=cut

# EndConduit
sub EndConduit
{
	# Do the necessary per-flavor cleanup
	if (($FLAVOR eq "fetch") or ($FLAVOR eq "sync"))
	{
		# XXX - Barf if $PDB undefined
		$PDB->Write($HEADERS{OutputDB}) or
			die "405 Can't write output database \"$HEADERS{OutputDB}\"\n";
	}
	# Nothing to do for "Dump" conduits

	print STDOUT "202 Success!\n";
	exit 0;
}

=item ConduitMain(I<flavor> => \&I<function>, ...)

Runs a multi-flavor conduit. Its arguments are a set of tuples of the form

	"fetch"	=> \&myFetchFunc,

specifying the function to call for each supported flavor. The
function reference on the right-hand side may be any code reference,
although in practice it is only practical to have references to
functions.

ConduitMain() performs the same initialization as StartConduit(): it
checks the command-line arguments, reads the headers from STDIN, and
makes sure that the flavor is one of those given in the arguments, and
that all of the mandatory headers were given.

It then calls the flavor-specific function given in the arguments, and
finally cleans up in the same way as EndConduit().

If the program is run not as a conduit but as a standalone program,
ConduitMain() supports the C<-config> option: when this option is
given, the program prints to STDOUT a set of sample configuration
entries that may be appended to F<.coldsyncrc>.

=cut

# ConduitMain
# 'main' function for multi-flavor conduits. Checks the arguments,
# reads the headers from STDIN, and runs the appropriate function.
# The arguments determine the appropriate function:
#	&ConduitMain(
#		flavor	=> \&flavorFunc,
#		flavor2 => \&flavor2Func,
#	);
sub ConduitMain
{
	my %flavors = @_;
	my $handler;			# Function that'll handle the
					# request

	# Sanity check: make sure all of the handlers are valid code
	# references
	for (keys %flavors)
	{
		if (ref($flavors{$_}) ne "CODE")
		{
			die "405 Invalid handler for \"$_\"\n";
		}
	}

	&ParseArgs(keys %flavors);	# Parse command-line arguments

	# Make sure the flavor given on the command line is supported
	if (exists($flavors{$FLAVOR}))
	{
		$handler = $flavors{$FLAVOR};
	} elsif (exists($flavors{ucfirst($FLAVOR)}))
	{
		$handler = $flavors{ucfirst($FLAVOR)};
	} else {
		die "403 Unsupported flavor\n";
	}

	ReadHeaders;

	$PDB = new Palm::PDB;
	if (defined($HEADERS{InputDB}))
	{
		$PDB->Load($HEADERS{InputDB}) or
			die "404 Can't read input database \"$HEADERS{InputDB}\"";
	}

	# Open the SPC pipe, if requested
	if (exists $ColdSync::SPC::{"VERSION"} and
	    defined $HEADERS{"SPCPipe"})
	{
		&ColdSync::SPC::spc_init;
	}

	# Call the appropriate handler. Note that $handler has to be
	# a hard reference, not a symbolic one.
	# XXX - Should this be inside an eval, to catch fatal errors in the
	# user's code?

	&{$handler} or die "501 Conduit failed\n";

	&EndConduit;
}

1;
__END__

Andrew Arensburger E<lt>arensb@ooblick.comE<gt>

=head1 SEE ALSO

Palm::PDB(1)

F<ColdSync Conduits: Specification and Hacker's Guide>

=cut
#'
# This is for Emacs's benefit:
# Local Variables:	***
# fill-column:	75	***
# End:			***
