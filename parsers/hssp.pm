package M6::Script::hssp;

our @ISA = "M6::Script";

sub new
{
	my $invocant = shift;
	my $self = new M6::Script(@_);
	return bless $self, "M6::Script::hssp";
}

sub parse
{
	my ($self, $text) = @_;

	open(my $h, "<", \$text);

	while (my $line = <$h>)
	{
		last if (substr($line, 0, 11) eq '## PROTEINS');
		
		my ($fld, $value) = ($1, $2) if $line =~ m/^(\S+?)\s+(.*)/;
		
		next unless defined $fld;
		next if $fld eq 'NOTATION';
		
		if ($fld eq 'PDBID')
		{
			$self->index_unique_string('id', $value);
			$self->set_attribute('id', $value);
		}
		elsif ($fld eq 'DBREF')
		{
			#DBREF      1CR7 A    1   236  PIR    S14765   S14765           1    236
			my $db = substr($line, 30,  7);	$db =~ s/\s+$//;
			my $ac = substr($line, 37,  9);	$ac =~ s/\s+$//; 
			my $id = substr($line, 46, 12);	$id =~ s/\s+$//;
			
			my %dbmap = (
				embl	=> 'embl',
				gb		=> 'genbank',
				ndb		=> 'ndb',
				pdb		=> 'pdb',
				pir		=> 'pir',
				prf		=> 'profile',
				sws		=> 'uniprot',
				trembl	=> 'uniprot',
				unp		=> 'uniprot'
			);

			$db = $dbmap{lc($db)} if defined $dbmap{lc($db)};
			
			if (length($db) > 0)
			{
				$self->add_link($db, $id) if length($id);
				$self->add_link($db, $ac) if length($ac);
			}
		}
		else
		{
			$value =~ s/(\w)\.(?=\w)/$1. /og
				if ($fld eq 'AUTHOR');

			$self->set_attribute('title', lc($value))
				if ($fld eq 'HEADER');

			my %fldmap = (
				'NALIGN'		=> 1,
				'SEQLENGTH'		=> 1,
				'NCHAIN'		=> 1,
				'KCHAIN'		=> 1
			);

			if (defined $fldmap{$fld}) {
				my $value = $1 if ($value =~ m/^(\d+)/);
				$self->index_number(lc $fld, $value);
			}
			else {
				$self->index_text('text', $value);
			}
		}
	}

	<$h>;
	while (my $line = <$h>)
	{
		last if (substr($line, 0, 13) eq '## ALIGNMENTS');
		
		my $id = substr($line, 8, 12); $id =~ s/\s+$//;
		my $strid = substr($line, 20, 4);
		my $acc = substr($line, 80, 10); $acc =~ s/\s+$//;
		
		$self->index_string('seqid', $id);
		$self->add_link('uniprot', $id);
		unless ($strid eq '    ')
		{
			$self->index_string('strid', $strid);
			$self->add_link('pdb', $strid);
		}
		$self->index_string('seqacc', $acc);
	}
}

1;
