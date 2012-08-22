package M6::Script::uniprot;

use utf8;

our @ISA = "M6::Script";

use strict;
use warnings;
use POSIX qw/strftime/;
use Data::Dumper;

our $commentLine1 = "-----------------------------------------------------------------------";
our $commentLine2 = "Copyrighted by the UniProt Consortium, see http://www.uniprot.org/terms";
our $commentLine3 = "Distributed under the Creative Commons Attribution-NoDerivs License";

our %INDICES = (
	id			=> 'Identification',
	ac			=> 'Accession number',
	cc			=> 'Comments and Notes',
	dt			=> 'Date',
	de			=> 'Description',
	gn			=> 'Gene name',
	os			=> 'Organism species',
	og			=> 'Organelle',
	oc			=> 'Organism classification',
	ox			=> 'Taxonomy cross-reference',
	'ref'		=> 'Any reference field',
	dr			=> 'Database cross-reference',
	kw			=> 'Keywords',
	ft			=> 'Feature table data',
	sv			=> 'Sequence version',
	fh			=> 'Feature table header',
	crc64		=> 'The CRC64 checksum for the sequence',
	'length'	=> 'The length of the sequence',
	mw			=> 'Molecular weight',
);

sub new
{
	my $invocant = shift;

#	my %merge_databanks = (
#		uniprot => [ 'sprot', 'trembl' ],
#		sp300	=> [ 'sp100', 'sp200' ],		# for debugging purposes
#	);

	my $self = new M6::Script(
		attr					=> [ 'title', 'acc' ],
		indices					=> \%INDICES,
		@_
	);
	
	return bless $self, "M6::Script::uniprot";
}

#sub version
#{
#	my ($self) = @_;
#
#	my $raw_dir = $self->{raw_dir} or die "raw_dir is not defined\n";
#	my $db = $self->{db} or die "db is not defined\n";
#	
#	$raw_dir =~ s|$db/?$|uniprot|;
#	my $version;
#
#	open RELDATE, "<$raw_dir/reldate.txt";
#	while (my $line = <RELDATE>)
#	{
#		if ($db eq 'sprot' and $line =~ /Swiss-Prot/) {
#			$version = $line;
#			last;
#		}
#		elsif ($db eq 'trembl' and $line =~ /TrEMBL/) {
#			$version = $line;
#			last;
#		}
#	}
#	close RELDATE;
#
#	$version = $self->SUPER::version() unless defined $version;
#	chomp($version);
#	
#	return $version;
#}

sub parse
{
	my ($self, $text) = @_;
	
	my %months = (
		'JAN' => 1, 'FEB' => 2, 'MAR' => 3, 'APR' => 4, 'MAY' => 5, 'JUN' => 6,
		'JUL' => 7, 'AUG' => 8, 'SEP' => 9, 'OCT' => 10, 'NOV' => 11, 'DEC' => 12
	);
	
	while ($text =~ m/^(?:([A-Z]{2})   ).+\n(?:\1.+\n)*/gm)
	{
		my $key = $1;
		my $value = $&;
		
		$value =~ s/^$key   //mg;

		if ($key eq 'ID')
		{
			$value =~ m/^(\w+)/ or die "No ID in UniProt record?\n$key   $value\n";
			$self->index_unique_string('id', $1);
			$self->set_attribute('id', $1);
		}
		elsif ($key eq 'AC')
		{
			my $n = 0;
			foreach my $ac (split(m/;\s*/, $value))
			{
				$self->index_unique_string('ac', $ac);
				$self->set_attribute('ac', $ac) unless ++$n > 1;
			}
		}
		elsif ($key eq 'DE')
		{
			$self->set_attribute('title', $1) if ($value =~ m/Full=(.+?);/);
			$self->index_text('de', $value);
#
#			if ($value =~ /(EC\s*)(\d+\.\d+\.\d+\.\d+)/)
#			{
#				$self->IndexLink('enzyme', $2);
#			}
		}
		elsif ($key eq 'DT')
		{
			if ($value =~ m/(\d{2})-([A-Z]{3})-(\d{4})/) {
				my $date = sprintf('%4.4d-%2.2d-%2.2d', $3, $months{$2}, $1);
				
				eval { $self->index_date('dt', $date); };
				
				warn $@ if $@;
			}
		}
		elsif ($key eq 'CC')
		{
			while ($value =~ m/.+/gm)
			{
				$self->index_text('cc', $&)
					unless ($& eq $commentLine1 or $& eq $commentLine2 or $& eq $commentLine3);
			}
		}
		elsif (substr($key, 0, 1) eq 'R')
		{
			$self->index_text('ref', $value);
		}
		elsif ($key eq 'DR')
		{
#			if ($value =~ m/\s*(.+?); (.+?); (.+?)\./)
#			{
#				my $db = $1;
#				$id = $2;
#
#				$id = $3 if ($links{$1}->{value} == 2);
#
#				$self->IndexLink($db, $id);
#			}
			
			$self->index_text('dr', $value);
		}
		elsif ($key eq 'SQ')
		{
			if ($value =~ /SEQUENCE\s+(\d+) AA;\s+(\d+) MW;\s+([0-9A-F]{16}) CRC64;/o)
			{
				$self->index_number('length', $1);
				$self->index_number('mw', $2);
				$self->index_string('crc64', $3);
			}
			
#			my $sequence = substr($text, pos $text);
#			$sequence =~ s/\s//g;
#			$sequence =~ s|//$||;
#			
#print $sequence, "\n";
		}
		elsif ($key ne 'XX')
		{
			$self->index_text(lc($key), $value);
		}
	}
}

1;
