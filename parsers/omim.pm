package M6::Script::omim;

our @ISA = "M6::Script";

sub new
{
	my $invocant = shift;
	my $self = new M6::Script(
		firstdocline => '*RECORD*',
		trailer => '*THEEND*',
		indices => {
			'no' => 'Number',
			'id' => 'Number',
			'ti' => 'Title',
			'mn' => 'Mini-Mim',
			'av' => 'Allelic variation',
			'tx' => 'Text',
			'sa' => 'See also',
			'rf' => 'References',
			'cs' => 'Clinical Synopsis',
			'cn' => 'Contributor name',
			'cd' => 'Creation name',
			'cd_date' => 'Creation date',
			'ed' => 'Edit history',
			'ed_date' => 'Edit history (date)',
		},
		@_
	);
	return bless $self, "M6::Script::omim";
}

sub parse
{
	my ($self, $text) = @_;
	
	while ($text =~ m/^\*FIELD\* (\w\w)\n((?:(?:\n|[^*\n].*\n)++|\*(?!FIELD).*\n)+)/mg)
	{
		my $key = $1;
		my $value = $2;
		
		if ($key eq 'NO')
		{
			$value =~ s/\s+$//;
			$self->index_unique_string('id', $value);
			$self->set_attribute('id', $value);
		}
		elsif ($key eq 'TI')
		{
			$self->index_text('title', $value);
			$value = $1 if $value =~ m/^[*#+%^]?[0-9]{6} (.+)/m ;
			$self->set_attribute('title', lc $value);
		}
		elsif ($key eq 'CD' or $key eq 'ED')
		{
			while ($value =~ m|^(.+?): (\d+)/(\d+)/(\d+)|gm)
			{
				my $date = sprintf('%4.4d-%2.2d-%2.2d', $4, $2, $3);

				$self->index_text(lc $key, $1);
#				$self->index_date(lc $key, $date);
				$self->index_string(lc($key) . '_date', $date);
			}
		}
		else
		{
			$self->index_text('text', $value);
		}
	}
}

1;
