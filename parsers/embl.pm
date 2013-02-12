package M6::Script::embl;

our @ISA = "M6::Script";

sub new
{
	my $invocant = shift;
	my $self = new M6::Script(
		lastdocline => '//',
		indices => {
			'id' => 'Identification',
			'ac' => 'Accession number',
			'co' => 'Contigs',
			'cc' => 'Comments and Notes',
			'dt' => 'Date',
			'pr' => 'Project',
			'de' => 'Description',
			'gn' => 'Gene name',
			'os' => 'Organism species',
			'og' => 'Organelle',
			'oc' => 'Organism classification',
			'ox' => 'Taxonomy cross-reference',
			'ref' => 'Any reference field',
			'dr' => 'Database cross-reference',
			'kw' => 'Keywords',
			'ft' => 'Feature table data',
			'sv' => 'Sequence version',
			'fh' => 'Feature table header',
			'topology'	=> 'Topology (circular or linear)',
			'mt' => 'Molecule type',
			'dc' => 'Data class',
			'td' => 'Taxonomic division',
			'length' => 'Sequence length'
		},
		@_
	);
	return bless $self, "M6::Script::embl";
}

sub parse
{
	my ($self, $text) = @_;

	my %skip = ( XX => 1, DT => 1, RN => 1, RP => 1, FH => 1 );
	
	while ($text =~ m/^(?:([A-Z]{2})   ).+\n(?:\1.+\n)*/mg)
	{
		my $key = $1;
		my $value = $&;
		$value =~ s/^$key   //gm;
		chomp($value);
		
		if ($key eq 'ID')
		{
			$value =~ s/(\w+);.*/$1/;
			$self->index_unique_string('id', $value);
			$self->set_attribute('id', $value);
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
			$self->index_text('de', $value);
			$value =~ s/;.+//;
			$self->set_attribute('title', substr($value, 0, 255));
		}
		elsif ($key eq 'DR')
		{
			while ($value =~ m/^(.+?); (.+?);/mg)
			{
				$self->add_link($1, $2);
			}
			
			$self->index_text('dr', $value);
		}
		elsif ($key eq 'FT')
		{
			while ($value =~ m/^\w+\s+((?:\S.+)(?:\n\s{15,}.+)*)/gm)
			{
				my $ft = $1;
				while ($ft =~ m'/(\w+)=(?|([^"].+)|"((?:[^"]++|"")*)")\n?'gm)
				{
					my $fkey = $1;
					my $fval = $2;

					next if ($fkey eq 'translation');

					$self->index_text('ft', $fval);

					if ($fkey eq 'db_xref' and $fval =~ m/^(.+?):(.+)$/)
					{
						$self->add_link($1, $2);
					}
				}
			}
		}
		elsif ($key eq 'SQ')
		{
			last;
		}
		elsif (not defined $skip{$key})
		{
			$self->index_text(lc $key, $value);
		}
	}
}

1;
