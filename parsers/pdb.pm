package M6::Script::pdb;

our @ISA = "M6::Script";

sub new
{
	my $invocant = shift;
	my $self = new M6::Script(@_);
	return bless $self, "M6::Script::pdb";
}

sub parse
{
	my ($self, $text) = @_;

	my ($state, %seq, $sequence, $seq_chain_id, $header, $compound, $title, $model_count, %ligands);

	while ($text =~ m/^.+$/mg)
	{
		my $line = $&;

		if ($line =~ /^HEADER\s+(.+?)\d{2}-[A-Z]{3}-\d{2}\s+(\S\S\S\S)/o)
		{
			$header = $1;
			
			$self->index_text('text', $1);
			
			my $id = $2;
			$self->index_unique_string('id', $id);

			$self->add_link('dssp', $id);
			$self->add_link('hssp', $id);
			$self->add_link('pdbfinder2', $id);

			$state = 1;
		}
		elsif ($state == 1)
		{
			chomp($line);
			
			if ($line =~ /^(\S+)\s+(\d+\s+)?(.+)/o)
			{
				my ($fld, $text) = ($1, $3);

				if ($fld eq 'MODEL')
				{
					$model_count = 1;
					$state = 2;
				}
				elsif ($fld eq 'ATOM')
				{
					$state = 2;
				}
				elsif ($fld eq 'TITLE')
				{
					$title .= lc $text;
					$self->index_text('title', $text);
				}
				elsif ($fld eq 'COMPND')
				{
					if ($text =~ m/MOLECULE: (.+)/) {
						$compound .= lc $1 . ' ';
					}
					elsif ($text =~ m/EC: (.+?);/) {
						my $ec = $1;
						foreach my $ecc (split(m/, /, $ec)) {
							$self->add_link('enzyme', $ecc);
						}	
						$compound .= 'EC: ' . lc $ec . ' ';
					}
					$self->index_text('compnd', $text);
				}
				elsif ($fld eq 'AUTHOR')
				{
					# split out the author name, otherwise users won't be able to find them
					
					$text =~ s/(\w)\.(?=\w)/$1. /og;
					$self->index_text('ref', $text);
				}
				elsif ($fld eq 'JRNL')
				{
					$self->index_text('ref', $text);
				}
				elsif ($fld eq 'REMARK')
				{
					if ($text =~ /\s*(\d+\s+)?AUTH\s+(.+)/o)
					{
						$text = $2;
						$text =~ s/(\w)\.(?=\w)/$1. /og;
					}
					
					$self->index_text('remark', $text);
				}
				elsif ($fld eq 'DBREF')
				{
# 0         1         2         3         4         5         6
# DBREF  2IGB A    1   179  UNP    P41007   PYRR_BACCL       1    179             
					my $db = substr($line, 26, 7);	$db =~ s/\s+$//;
					my $ac = substr($line, 33, 9);	$ac =~ s/\s+$//;
					my $id = substr($line, 42, 12);	$id =~ s/\s+$//;
					
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
				elsif ($fld eq 'SEQRES')
				{
					my $chain_id = substr($line, 11, 1);
					my $s = substr($line, 19, 52);
					
					if (defined $seq_chain_id and
						defined $chain_id and
						$chain_id ne $seq_chain_id and
						defined $sequence)
					{
						if (length($sequence) > 2) {
							$seq{$seq_chain_id} = $sequence;
						}
						$sequence = undef;
					}

					$seq_chain_id = $chain_id;
					
					foreach my $aa (split(m/\s+/, $s))
					{
						if (length($aa) == 3)
						{
							$sequence = "" unless defined $sequence;
							if (defined $aa_map{$aa})
							{
								$sequence .= $aa_map{$aa};
							}
							else
							{
								$sequence .= 'X';		# unknown, avoid gaps
							}
						}
					}
				}
				elsif ($line =~ /^\S+\s+(.*)/o)
				{
					$self->index_text('text', $1);
				}
			}
		}
		elsif ($state == 2)
		{
			$doc .= $line;
			chomp($line);
			
			my $fld = substr($line, 0, 6);
			if ($fld eq 'HETATM') {
				my $ligand = substr($line, 17, 3);
				$ligands{$ligand} = 1;
			}
			elsif ($fld eq 'MODEL ') {
				++$model_count;
			}
		}
	}

	$self->index_number('models', $model_count) if defined $model_count;
	$self->set_attribute('title', &GetTitle($header, $title, $compound));

	foreach my $ligand (keys %ligands)
	{
		$ligand =~ s/^\s*(\S+)\s*$/$1/;
		$self->index_string("ligand", $ligand);
	}

#	foreach my $ch (sort keys %seq)
#	{
#		$self->AddSequence($seq{$ch}, $ch);
#		delete $seq{$ch};
#	}
}

1;
