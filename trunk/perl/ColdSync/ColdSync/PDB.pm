package ColdSync::PDB;

use 5.006001;
use strict;
use warnings;

use Palm::PDB;
use Palm::StdAppInfo;
use ColdSync::SPC;

$ColdSync::PDB::VERSION = do { my @r = (q$Revision: 1.1 $ =~ /\d+/g); sprintf "%d."."%02d" x $#r, @r };

=head1 NAME

ColdSync::PDB - Manage a Palm database via the DLP protocol using
                Palm::PDB helpers.

=head1 SYNOPSIS

	use ColdSync::SPC;
	use ColdSync::PDB;
	use Palm::Mail;

	my $db = ColdSync::PDB->Current("r");
	my $catno = $db->catno('Inbox');
	while (my $record = $db->nextRecInCategory($catno))
	{
		print "Email from $record->{'from'}\n";
	}

=head1 DESCRIPTION

The Palm::PDB module provides hooks allowing helper modules to handle
the application specific details of Palm database files.  ColdSync::PDB
extends the Palm::PDB helper support to remote databases accessed during
a sync.

Due to limitations in the DLP protocol (actually, it appears more likely to
be a PalmOS implementation limit), only a single database can be open at
any one time. As well, due to the remote communications issues, some
of the Palm::PDB approaches to database access aren't terribly efficient.
Specifically, fetching the entire record list in one call is a bad idea
for most sync conduits.

=head1 METHODS

=head2 Current

	my $db = ColdSync::PDB->Current($mode);

Contructs a new ColdSync::PDB object from the database currently being
synced on the Palm.

C<$mode> indicates how to open the database. It's a combination
of characters indicating the desired access:

	r  open for reading
	w  open for writing
	x  exclusive access
	s  show secret records

=cut
sub Current($$)
{
	my ($class, $mode) = @_;

	my $self = spc_get_dbinfo();
	return undef unless defined $self;

	bless $self, $class;

	my $m = 0;
	$m |= 0x80 if $mode =~ /r/;
	$m |= 0x40 if $mode =~ /w/;
	$m |= 0x20 if $mode =~ /x/;
	$m |= 0x10 if $mode =~ /s/;

	$self->{'dbhandle'} = dlp_OpenDB($self->{'name'}, $m);
	return undef unless defined $self->{'dbhandle'};

	$self->{'mode'} = $mode;
	$self->{'modebits'} = $m;

	# setup the Palm::PDB helper, if any
	$self->_init_helper();
	return undef unless defined $self->{'helper'};

	$self->_init_app_info();

	return $self;
}

=head2 Load

	my $db = ColdSync::PDB->Load($dbname, $mode);

Creates a handle to the newly opened database C<$dbname>. This constructor
is, compared to ColdSync::PDB::Current(), extremely slow since it has to
search the database list in order to find things like creator and type
information.

C<$mode> indicates how to open the database. It's a combination
of characters indicating the desired access:

	r  open for reading
	w  open for writing
	x  exclusive access
	s  show secret records

=cut
sub Load($$$)
{
	my ($class, $dbname, $mode) = @_;

	my $self = dlp_FindDBByName($dbname);
	return undef unless defined $self;

	bless $self, $class;

	# XXX PDB ctime/mtime/backtime are EPOCH, not hashes
	# XXX db_flags are attributes?

	my $m = 0;
	$m |= 0x80 if $mode =~ /r/;
	$m |= 0x40 if $mode =~ /w/;
	$m |= 0x20 if $mode =~ /x/;
	$m |= 0x10 if $mode =~ /s/;

	$self->{'dbhandle'} = dlp_OpenDB($self->{'name'}, $m);
	return undef unless defined $self->{'dbhandle'};

	$self->{'mode'} = $mode;
	$self->{'modebits'} = $m;

	# setup the Palm::PDB helper, if any
	$self->_init_helper();
	return undef unless defined $self->{'helper'};

	$self->_init_app_info();

	return $self;
}

sub DESTROY($)
{
	my $self = shift;
	dlp_CloseDB($self->{'dbhandle'}) if defined $self->{'dbhandle'};
}

sub _init_helper($)
{
	my $self = shift;

	# Find the appropriate helper for this database
	$self->{'helper'} =
		$Palm::PDB::PDBHandlers{$self->{creator}}{$self->{type}} ||
		$Palm::PDB::PDBHandlers{undef}{$self->{type}} ||
		$Palm::PDB::PDBHandlers{$self->{creator}}{""} ||
		$Palm::PDB::PDBHandlers{""}{""} || undef;
}

sub _init_app_info($)
{
	my $self = shift;

	my $data = dlp_ReadAppBlock($self->{'dbhandle'});
	return unless defined $data;
	$self->{'appinfo'} = $self->{'helper'}->ParseAppInfoBlock($data);
}

sub _calc_record_attributes($)
{
	my $record = shift;

	# snarfed from Palm::PDB
	my $attributes = 0;
	if ($record->{attributes}{expunged} || $record->{attributes}{deleted}) {
		# archive during next sync
		$attributes |= 0x08 if $record->{attributes}{archive};
	} else {
		$attributes = ($record->{category} & 0x0f);
	}
	$attributes |= 0x80 if $record->{attributes}{expunged};
	$attributes |= 0x40 if $record->{attributes}{dirty};
	$attributes |= 0x20 if $record->{attributes}{deleted};
	$attributes |= 0x10 if $record->{attributes}{private};

	$attributes |= 0x80 if $record->{'attributes'}{'Delete'};
	$attributes |= 0x40 if $record->{'attributes'}{'Dirty'};
	$attributes |= 0x20 if $record->{'attributes'}{'Busy'};
	$attributes |= 0x10 if $record->{'attributes'}{'Secret'};

	return $attributes;
}

=head2 isReadable
	
	read_database() if $db->isReadable();

Returns true if the database is open for reading.

=cut
sub isReadable($)
{
	my $self = shift;
	die "isReadable() is an instance method" unless ref $self;
	return $self->{modebits} & 0x80;
}

=head2 isWritable
	
	$db->deleteRecord($record) if $db->isWritable();

Returns true if the database is open for writing.

=cut
sub isWritable($)
{
	my $self = shift;
	die "isWritable() is an instance method" unless ref $self;
	return $self->{modebits} & 0x40;
}

=head2 catno

	my $catno = $db->catno($catname);

Map the category name C<$catname> to an index into the databases category
list or return undef if it can't be found.

This isn't a DLP or Palm::PDB method, but it's something that a lot of
conduits have to do.

=cut
sub catno($$)
{
	my ($self,$catname) = @_;
	die "catno() is an instance method" unless ref $self;

	for (my $i = 0; $i < Palm::StdAppInfo::numCategories; $i ++)
	{
		my $category = $self->{'appinfo'}{'categories'}[$i];
		next unless defined $category;
		return $category->{'id'} if $category->{'name'} eq $catname;
	}
	return undef;
}

=head2 records

	my @records = $db->records();
	foreach (@records)
	{
		process_record($_);
	}

Returns all the records in the database. More specifically, it
B<downloads> and returns all the records. And it doesn't cache. Depending
on the size of the database and the speed of the link, this can be a
very expensive operation. Only use it if you're sure you want B<all>
the records in the database. It's usually sufficient to try fetching
modified records, records in a specific category, or (if synching with
a local source) accessing records by a known ID.

=cut
sub records($)
{
	my $self = shift;
	die "records() is an instance method" unless ref $self;

	die "records() called on unreadable database" unless $self->isReadable();

	my $info = dlp_ReadOpenDBInfo($self->{'dbhandle'});
	return () unless defined $info and $info->{numrecords} > 0;

	# XXX if records were treated as the right kind of objects
	# (tied hashes, probably), we could just load the record Id list
	# and Dlp request the record bits when the record fields were accessed,
	# caching as we go. This would require a sophisticated record
	# cache in order to handle things like deletions and writes and
	# some extra smarts to know when the database went away.

	my @records;
	for (my $i = 0; $i < $info->{numrecords}; $i ++)
	{
		my $rawrec = dlp_ReadRecordByIndex($self->{'dbhandle'}, $i, 0, -1);
		next unless defined $rawrec;

		my $rec = $self->{'helper'}->ParseRecord(%$rawrec);
		push @records, $rec if defined $rec;
	}
	return @records;
}

=head2 nextRecInCategory

	my $catno = $db->catno('Inbox');
	while(my $record = $db->nextRecInCategory($catno))
	{
		process_record($record);
	}

Fetch the next record in the category index C<$catno>.  Returns undef
when no more records are available. The C<resetIndex> method will reset
the index back to the beginning.

Mixing different calling modes (modified/unmodified,category/any) may
give some undefined results. If you're planning on changing records
while iterating, check out the Sync Manager API documentation
(http://www.palmos.com/dev/support/docs/conduits/win/CComp_SyncMgr.html#970971).

=cut
sub nextRecInCategory($$)
{
	my ($self,$catno) = @_;
	die "nextRec() is an instance method" unless ref $self;
	die "nextRecInCategory() called on unreadable database"
		unless $self->isReadable();

	my $recordraw = dlp_ReadNextRecInCategory($self->{'dbhandle'}, $catno);
	return undef unless defined $recordraw;

	return $self->{'helper'}->ParseRecord(%$recordraw);
}

=head2 nextModifiedRec

	my $record = $db->nextModifiedRec($catno);

Fetch the next modified record in the category index C<$catno>. If
C<$catno> is undef, it will fetch the next modified record in any
category.  Returns undef when no more records are available. The C<resetIndex>
method will reset the index back to the beginning.

Mixing different calling modes (modified/unmodified,category/any) may
give some undefined results. If you're planning on changing records
while iterating, check out the Sync Manager API documentation
(http://www.palmos.com/dev/support/docs/conduits/win/CComp_SyncMgr.html#970971).

=cut
sub nextModifiedRec($@)
{
	my ($self,$catno) = @_;
	die "nextModifiedRec() is an instance method" unless ref $self;

	die "nextModifiedRec() called on unreadable database"
		unless $self->isReadable();

	my $recordraw;
	if (defined $catno)
	{
		$recordraw = dlp_ReadNextModifiedRecInCategory(
			$self->{'dbhandle'}, $catno);
	} else
	{
		$recordraw = dlp_ReadNextModifiedRec($self->{'dbhandle'});
	}
	return undef unless defined $recordraw;

	return $self->{'helper'}->ParseRecord(%$recordraw);
}

=head2 resetIndex

	$db->resetIndex();

Resets the modified/category index.

=cut
sub resetIndex($)
{
	my $self = shift;
	die "resetIndex() is an instance method" unless ref $self;
	dlp_ResetRecordIndex($self->{'dbhandle'});
}

=head2 deleteRecord

	$db->deleteRecord($record);

Deletes the specified C<$record>.

=cut
sub deleteRecord($$@)
{
	my ($self,$record,$flags) = @_;
	die "deleteRecord() is an instance method" unless ref $self;

	die "deleteRecord() called on unreadable database"
		unless $self->isReadable();
	die "deleteRecord() called on unwritable database"
		unless $self->isWritable();

	dlp_DeleteRecord($self->{dbhandle}, $record->{id});
}

=head2 deleteAllRecords

	$db->deleteAllRecords();

Deletes B<all> records in the database.

=cut
sub deleteAllRecords($)
{
	my $self = shift;
	die "deleteAllRecords() is an instance method" unless ref $self;
	die "deleteAllRecords() called on unwritable database"
		unless $self->isWritable();

	dlp_DeleteAllRecords($self->{dbhandle});
}

=head2 writeRecord

	$db->writeRecord($record);

Writes the given record to the database. If the record already exists (i.e.
has a non-zero id) it will simply be modified. If it doesn't exist, it
will be appended.

The C<id> field of the C<$record> will be modified to the new record id.

=cut
sub writeRecord($$)
{
	my ($self,$record) = @_;
	die "writeRecord() is an instance method" unless ref $self;

	die "writeRecord() called on unreadable database"
		unless $self->isReadable();
	die "writeRecord() called on unwritable database"
		unless $self->isWritable();

	my $data = $self->{'helper'}->PackRecord($record);
	return unless defined $data and length $data > 0;

	# adjust attributes
	my $attributes = _calc_record_attributes($record);

	my ($err,$rv,$nid) = dlp_WriteRecord($self->{dbhandle},
		$record->{id}, $data, $record->{category}, $attributes, 0);
	$record->{id} = $nid if $err != 0;
}

=head2 newRecord

	my $record = $db->newRecord();
	$db->writeRecord($record);

Creates a new record of the appropriate helper type, filling in all the
appropriate defaults. Note that this is simply an empty local template. The
record does not exist in the remote database and will not exist until
a C<writeRecord> method is called for it.

=cut
sub newRecord($)
{
	return $_[0]->{'helper'}->new_Record();
}

=head2 findRecordByID
	
	$record = $db->findRecordByID($id);

Find a record in the database with record ID C<$id>. If the record doesn't
exist, undef is returned.

=cut
sub findRecordByID($$)
{
	my ($self,$id) = @_;
	die "findRecordByID() is an instance method" unless ref $self;

	die "findRecordByID() called on unreadable database"
		unless $self->isReadable();

	return undef unless $id > 0;

	my $recordraw = dlp_ReadRecordById($self->{'dbhandle'}, $id, 0, -1);
	return undef unless defined $recordraw;

	return $self->{'helper'}->ParseRecord(%$recordraw);
}

=head1 BUGS

I'm fairly sure that the write attribute setting doesn't really work
properly.

Resource databases aren't handled.

Certain things like mucking with the appinfo block is not supported.

The C<Load> method uses C<dlp_FindDBByName> which limits us to Palm OS
3.0 and up.

=head1 SEE ALSO

coldsync(8)

Palm::PDB

ColdSync::SPC

Palm OS C/C++ Sync Suite Companion:
http://www.palmos.com/dev/support/docs/conduits/win/C++SyncCompanionTOC.html

=head1 AUTHOR

Christophe Beauregard, E<lt>christophe.beauregard@sympatico.caE<gt>

=cut
