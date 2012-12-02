package M6::Script::genbank;

our @ISA = "M6::Script";

sub new
{
	my $invocant = shift;
	my $self = new M6::Script(
		firstdocline => qr/^LOCUS.+/,
		lastdocline => '//',
		@_
	);
	return bless $self, "M6::Script::genbank";
}

sub parse
{
	my ($self, $text) = @_;

	# watch out, recursion in subexpressions is limited to 32k lines.
	# that's not enough for genbank records containing complete genomes...
	foreach my $part (split(m/\n(?=\w)/, $text))
	{
		next unless ($part =~ m/^(\w+)\s+/);
		
		my $key = lc $1;
		my $value = substr($part, $+[0]);

		if ($key eq 'locus')
		{
			my $id = substr($value, 0, 15);
			$id =~ s/\s+$//;
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
		elsif ($key eq 'reference')
		{
			while ($value =~ m/^  (\w+)\s+((?:\S.+)(?:\n\s{12,}.+)*)/mg)
			{
				my $rkey = lc $1;
				my $rvalue = $2;

				$self->index_text($rkey, $rvalue);
			}
		}
		elsif ($key eq 'features')
		{
			foreach my $feature (split(m/\n {5}(?=\w)/, $part))
			{
				while ($feature =~ m'/(\w+)=(?|([^"].+)|"((?:[^"]++|"")*)")\n?'gm)
				{
					my $fkey = $1;
					my $fval = $2;

					if ($fkey ne 'translation')
					{
						$self->index_text('feature', $fval);
	
						if ($fkey eq 'db_xref' and $fval =~ m/(.+?):(.+)/)
						{
							$self->add_link($1, $2);
						}
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
