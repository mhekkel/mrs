#!/usr/bin/perl
#
# Script to generate compressed C structures with Unicode information
# for all valid unicode point.
#
# This is a stripped down version of Japi's table generator

use strict;
use warnings;
use Data::Dumper;

$| = 1;

my $kUC_COUNT = 1114112;
my $kUC_PAGE_COUNT = 4352;

my (%codes, @data);

print STDERR "Initializing array...";

for (my $uc = 0; $uc < $kUC_COUNT; ++$uc) {
	my %ucd = (
		prop	=> 'kOTHER',
		ccc		=> 0,
		lower	=> 0,
		1	=> 0,
		2	=> 0,
	);
	
	push @data, \%ucd;
}

print STDERR " done\n";

my $re = qr/^([0-9A-F]{4,5})(\.\.([0-9A-F]{4,5}))?\s*;\s*(\S+)/;

# Character Property Table

print STDERR "Reading UnicodeData.txt ...";

open(my $h, "<UnicodeData.txt") or die "Could not open UnicodeData.txt\n";
while (my $line = <$h>) {
	chomp($line);
	my @v = split(m/;/, $line);
	
	next unless scalar(@v) > 3;
	
	my $uc = hex($v[0]);

	my $c1 = substr($v[2], 0, 1);
	my $c2 = substr($v[2], 1, 1);
	
	# character class

	if ($c1 eq 'L') {
		$data[$uc]->{'prop'} = 'kLETTER';
	}
	elsif ($c1 eq 'N') {
		$data[$uc]->{'prop'} = 'kNUMBER';
	}
	elsif ($c1 eq 'M') {
		$data[$uc]->{'prop'} = 'kCOMBININGMARK';
	}
	elsif ($c1 eq 'P') {
		$data[$uc]->{'prop'} = 'kPUNCTUATION';
	}
	elsif ($c1 eq 'S') {
		$data[$uc]->{'prop'} = 'kSYMBOL';
	}
	elsif ($c1 eq 'Z') {
		$data[$uc]->{'prop'} = 'kSEPARATOR';
	}
	elsif ($c1 eq 'C') {
		$data[$uc]->{'prop'} = 'kCONTROL';
	}
	
	if (my $r = $v[5]) {
		my ($s, $a, $b) = $r =~ m/(?:(<\w+>)\s)?([0-9A-F]{4})(?:\s([0-9A-F]{4}))?/;
		$b = 0 unless $b;
		
		# we don't do compatible decomposition for now...
		unless ($s) {
			$data[$uc]->{1} = hex($a);
			$data[$uc]->{2} = hex($b);
		}
	}
	
	$data[$uc]->{ccc} = int($v[3]);
}
close($h);

print STDERR " done\n";

for (my $uc = hex('0x3400'); $uc <= hex('0x4DB5'); ++$uc) {
	$data[$uc]->{'prop'} = 'kLETTER';
}

for (my $uc = hex('0x4E00'); $uc <= hex('0x09FA5'); ++$uc) {
	$data[$uc]->{'prop'} = 'kLETTER';
}

for (my $uc = hex('0x0Ac00'); $uc <= hex('0x0D7A3'); ++$uc) {
	$data[$uc]->{'prop'} = 'kLETTER';
}

for (my $uc = hex('0x20000'); $uc <= hex('0x2A6D6'); ++$uc) {
	$data[$uc]->{'prop'} = 'kLETTER';
}

# we treat an underscore as a letter
$data[95]->{'prop'} = 'kLETTER';

# Read the case folding table

my %folded;
open($h, "<CaseFolding.txt") or die "Could not open CaseFolding.txt\n";
while (my $line = <$h>) {
	next if substr($line, 0, 1) eq '#';
	chomp($line);
	my @v = split(m/;\s*/, $line);
	
	next unless scalar(@v) >= 3;
	next unless $v[1] eq 'F' or $v[1] eq 'C';

	my $uc = hex($v[0]);
	my @l = split(m/\s/, $v[2]);
	
	if (scalar(@l) == 1) {
		$data[$uc]->{'lower'} = hex($l[0]);
	}
	else {
		$data[$uc]->{'lower'} = 1;
		$folded{$uc} = \@l;
	}
}
close($h);

# now build the tables

# table 1, the character break class information

my $UnicodeInfoMapping = <<EOF;

enum M6UnicodeProperty : uint8
{
	kLETTER			= 0,
	kNUMBER			= 1,
	kCOMBININGMARK	= 2,
	kPUNCTUATION	= 3,
	kSYMBOL			= 4,
	kSEPARATOR		= 5,
	kCONTROL		= 6,
	kOTHER			= 7
};

struct M6UnicodeInfoAtom
{
	unsigned int	ccc		: 8;	// canonical combining class
	unsigned int	prop	: 3;	// unicode property
	unsigned int	lower	: 20;
};

// BOOST_STATIC_ASSERT(sizeof(M6UnicodeInfoAtom) == sizeof(uint32));

typedef M6UnicodeInfoAtom	M6UnicodeInfoPage[256];

struct M6UnicodeInfo {
	int16				page_index[%d];
	M6UnicodeInfoPage	data[%d];
} kM6UnicodeInfo = {
EOF

my $table = new Table(header => $UnicodeInfoMapping);

foreach my $pageNr (0 .. $kUC_PAGE_COUNT - 1)
{
	my $page = "\t\t{\n";
	
	foreach my $uc ($pageNr * 256 .. ($pageNr + 1) * 256 - 1) {
		$page .= sprintf("\t\t\t{%d, %s, 0x%5.5x },\n",
			$data[$uc]->{'ccc'}, $data[$uc]->{'prop'}, $data[$uc]->{'lower'});
	}
	
	$page .= "\t\t},\n";
	
	$table->addPage($pageNr, $page);
}

$table->print_out();

# table 2, the mapping from Composed to Decomposed

my $ComposedDecomposedMapping = <<EOF;

typedef uint16				M6NormalisedForm[2];
typedef M6NormalisedForm	M6NormalisedPage[256];

struct M6NormalisationInfo
{
	int16				page_index[%d];
	M6NormalisedPage	data[%d];
} kM6NormalisationInfo = {
EOF

$table = new Table(header => $ComposedDecomposedMapping);

foreach my $pageNr (0 .. $kUC_PAGE_COUNT - 1)
{
	my $page = "\t\t{\n";
	
	foreach my $uc ($pageNr * 256 .. ($pageNr + 1) * 256 - 1) {
		$page .= sprintf("\t\t\t { %s, %s },\n",
			$data[$uc]->{1}, $data[$uc]->{2});
	}
	
	$page .= "\t\t},\n";
	
	$table->addPage($pageNr, $page);
}

$table->print_out();

# and the remaining folds

print <<EOF;

struct M6FullCaseFold
{
	uint32			uc;
	uint32			folded[3];
} kM6FullCaseFolds[] = {
EOF

foreach my $fold (sort { int($a) <=> int($b) } keys %folded) {
	printf "\t0x%5.5x, { %s },\n", $fold,
		join(", ", map { sprintf("0x%s", $_) } @{$folded{$fold}});
}

print "};\n";

exit;

package Table;

sub new
{
	my $invocant = shift;
	my $self = {
		indexCount	=> 0,
		pageIndex	=> [],
		pages		=> [],
		@_
	};
	
	return bless $self, "Table";
}

sub addPage
{
	my ($self, $pageNr, $page) = @_;
	
	my $pageIx = -1;
	
	for (my $pn = 0; $pn < scalar($self->{'pages'}); ++$pn)
	{
		last unless defined @{$self->{'pages'}}[$pn];

		if (@{$self->{'pages'}}[$pn] eq $page)
		{
			$pageIx = $pn;
			last;
		}
	}
	
	if ($pageIx == -1)
	{
		$pageIx = push @{$self->{'pages'}}, $page;
		$pageIx -= 1;
	}
	
	push @{$self->{'pageIndex'}}, $pageIx;
}

sub print_out
{
	my $self = shift;
	
	my $pageIndexSize = scalar(@{$self->{'pageIndex'}});
	my $pageCount = scalar(@{$self->{'pages'}});
	
	die "Hey, geen header!\n" unless $self->{header};
	printf $self->{header}, $pageIndexSize, $pageCount;

	print "\t{";
	
	# the index
	
	for (my $ix = 0; $ix < $pageIndexSize; ++$ix)
	{
		print "\n\t\t" if ($ix % 16) == 0;
		printf "%3d,", @{$self->{'pageIndex'}}[$ix];
	}
	
	print "\n\t},\n\t{\n";

	# the data pages

	my $n = 0;
	foreach my $page (@{$self->{'pages'}}) {
		print "// page $n\n"; ++$n;
		print $page;
	}

	print "\n\t}\n};\n";
}

