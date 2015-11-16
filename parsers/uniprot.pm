package M6::Script::uniprot;

our @ISA = "M6::Script";

use strict;
use warnings;

sub new
{
	my $invocant = shift;
	my $self = new M6::Script(
		lastdocline => '//',
		indices => {
			ac			=> 'Accession number',
			cc			=> 'Comments and Notes',
			crc64		=> 'The CRC64 checksum for the sequence',
			de			=> 'Description',
			doi			=> 'Digital Object Indentifier (reference)',
			dr			=> 'Database cross-reference',
			dt			=> 'Date',
			ft			=> 'Feature table data',
			gn			=> 'Gene name',
			id			=> 'Identification',
			kw			=> 'Keywords',
			'length'	=> 'The length of the sequence',
			medline		=> 'Medline reference',
			mw			=> 'Molecular weight',
			oc			=> 'Organism classification',
			og			=> 'Organelle',
			oh			=> 'Organism host',
			os			=> 'Organism species',
			ox			=> 'Taxonomy cross-reference',
			pe			=> 'Protein Existence (evidence)',
			pubmed		=> 'PubMed reference'
		},
		@_
	);
	return bless $self, "M6::Script::uniprot";
}

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
				$self->index_string('ac', $ac);
				$self->set_attribute('ac', $ac) unless ++$n > 1;
			}
		}
		elsif ($key eq 'DE')
		{
			$self->set_attribute('title', $1) if ($value =~ m/Full=(.+?);/);
			$self->index_text('de', $value);

			while ($value =~ /(EC=)(\d+\.\d+\.\d+\.\d+)/g)
			{
				$self->add_link('enzyme', $2);
			}
		}
		elsif ($key eq 'DT')
		{
			while ($value =~ m/(\d{2})-(JAN|FEB|MAR|APR|MAY|JUN|JUL|AUG|SEP|OCT|NOV|DEC)-(\d{4})/g)
			{
				my $date = sprintf('%4.4d-%2.2d-%2.2d', $3, $months{$2}, $1);
#				$self->index_date('dt', $date);
				$self->index_string('dt', $date);
			}
		}
		elsif ($key eq 'GN')
		{
			while ($value =~ m/(?:\w+=)?(.+);/g)
			{
				$self->index_text(lc $key, $1);
			}
		}
		elsif ($key eq 'OC' or $key eq 'KW')
		{
			while ($value =~ m/(.+);/g)
			{
				$self->index_string(lc $key, $1);
			}
		}
		elsif ($key eq 'CC')
		{
			while ($value =~ m/.+/gm)
			{
				$self->index_text('cc', $&)
					unless ($& eq "-----------------------------------------------------------------------" or
							$& eq "Copyrighted by the UniProt Consortium, see http://www.uniprot.org/terms" or
							$& eq "Distributed under the Creative Commons Attribution-NoDerivs License");
			}
		}
		elsif ($key eq 'RX')
		{
			while ($value =~ m/(MEDLINE|PubMed|DOI)=([^;]+);/g)
			{
				$self->index_string(lc $1, $2);
			}
		}
		elsif (substr($key, 0, 1) eq 'R')
		{
			$self->index_text('ref', $value);
		}
		elsif ($key eq 'DR')
		{
			while ($value =~ m/^(.+?); (.+?);(?: (.+?);)?/mg)
			{
				my ($db, $id, $ac) = ($1, $2, $3);
				
				my $ldb = lc $db;
				
				if ($ldb eq 'prosite' or $ldb eq 'pfam')
				{
					$id = $ac;
				}
				elsif ($ldb eq 'refseq')
				{
					$id =~ s/\.\d+$//;
				}
				elsif ($ldb eq 'go')
				{
					$id = substr($id, 3);
				}
				$self->add_link($db, $id);
			}
			
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
#			$self->add_sequence($sequence);
			
			last;
		}
		elsif ($key ne 'XX')
		{
			$self->index_text(lc($key), $value);
		}
	}
}

sub to_fasta
{
	my ($self, $doc, $db, $id, $title) = @_;

	die("no sequence") unless
		$doc =~ /SEQUENCE\s+(\d+) AA;\s+\d+ MW;\s+[0-9A-F]{16} CRC64;\n((\s+.+\n)+)\/\//m ;

	my $len = $1;
	my $seq = $2;
	
	$seq =~ s/\s//g;
	$seq =~ s|//$||;
	
	die (sprintf("invalid SEQUENCE record %d!=%d", length($seq), $len))
		unless (length($seq) == $len);
	
	$seq =~ s/.{72}/$&\n/g;

	my @acs=();
	while ($doc =~ m/^AC\s+([A-Z0-9\-\s;]+)+\n/gm)
	{
		my $value=$1 ;
		foreach my $ac (split(m/;\s*/, $value))
		{
			push(@acs,$ac) ;
		}
	}

#	if (scalar @acs)
#	{
#		my $result = '';
#		foreach my $ac (@acs)
#		{
#			$result .= ">gnl|$db|$id|$ac $title\n$seq\n";
#		}
#		return $result;
#	}
#	else
#	{
	return ">gnl|$db|$id $title\n$seq\n";
#	}
}

sub version
{
	my ($self, $source) = @_;
	
	return undef;
}

1;
