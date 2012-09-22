package M6::Script::genbank;

our @ISA = "M6::Script";

sub new
{
	my $invocant = shift;
	my $self = new M6::Script(
		lastdocline => '//',
		@_
	);
	return bless $self, "M6::Script::genbank";
}

sub parse
{
	my ($self, $text) = @_;

	while ($text =~ m/^\s*(\w+)\s+((?:.+)(?:\n\s{5,}.+)*)/mg)
	{
		my $key = lc $1;
		my $value = $2;
#		$value =~ s/^\s{5,}//gm;
		
		if ($key eq 'locus')
		{
			my $id = substr($value, 0, 15);
			$self->index_unique_string('id', $id);
			$self->set_attribute('id', $id);
			
			$self->index_string('division', substr($value, 52, 3));
		}
		elsif ($key eq 'definition')
		{
			$value =~ s/\s+/ /g;
			$value =~ s/;.+//;
			$self->index_text('title', $value);
			$self->set_attribute('title', substr($value, 0, 255));
		}
		elsif ($key eq 'accession')
		{
			$self->index_string('accession', $&)
				while ($value =~ m/\S+/g);
		}
		elsif ($key eq 'version')
		{
			if ($value =~ m/(\S+)\s+GI:(\d+)/)
			{
				$self->index_unique_string('version', $1);
				$self->index_unique_string('gi', $2);
			}
		}
		elsif ($key eq 'keywords')
		{
			$value =~ s/\.$//;
			foreach my $kw (split(m/;\s*/, $value))
			{
				$self->index_string('keywords', $kw);
			}
		}
		elsif ($key eq 'features')
		{
			while ($value =~ m/^\s{5}\w+\s+((?:\S.+)(?:\n\s{20,}.+)*)/gm)
			{
				my $feature = $1;
				while ($feature =~ m'/(\w+)=(?|([^"].+)|"((?:[^"]++|"")*)")\n?'gm)
				{
					my $fkey = $1;
					my $fval = $2;

					next if ($fkey eq 'translation');
					if ($fkey eq 'db_xref')
					{
						$self->add_link(split(m/:/, $fval));
					}
					else
					{
						$self->index_text('feature', $fval);
					}
				}
			}
		}
		elsif ($key eq 'origin')
		{
			last;
		}
		else
		{
			$self->index_text(lc $key, $value);
		}
	}
}

1;
