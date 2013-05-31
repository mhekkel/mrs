package M6::Script::enzyme;

our @ISA = "M6::Script";

sub new
{
	my $invocant = shift;
	my $self = new M6::Script(
		lastdocline => '//',
		indices => {
			'id' => 'Identification',
			'de' =>	'Description',
			'an' => 'Alternate Name',
			'ca' => 'Catalytic Activity',
			'cf' => 'CoFactor',
			'cc' => 'Comments',
			'di' => 'Disease',
			'pr' => 'Prosite Reference',
			'dr' => 'Database Reference'
		},
		@_
	);
	return bless $self, "M6::Script::enzyme";
}

sub parse
{
	my ($self, $text) = @_;

	while ($text =~ m/^(?:([A-Z]{2})   ).+\n(?:\1.+\n)*/mg)
	{
		my $key = $1;
		my $value = $&;

		chomp($value);
		
		$value =~ s/^$key   //gm;
		
		if ($key eq 'ID')
		{
			$value = $1 if $value =~ m/(\d+\.\d+\.\d+\.\d+)/;
			$self->index_unique_string('id', $value);
			$self->set_attribute('id', $value);
		}
		elsif ($key eq 'DE')
		{
			$self->index_text('de', $value);
			$value = substr($value, 0, 255) if length($value) > 255;
			$self->set_attribute('title', lc $value);
		}
		elsif ($key eq 'DR')
		{
			$self->index_text('dr', $value);
			while ($value =~ m/(\w+?), (\w+) ;/g)
			{
				$self->add_link('sprot', $2);
			}
		}
		elsif ($key eq 'PR')
		{
			$value = $1 if $value =~ m/PROSITE; (.+)/;
			$self->index_text('pr', $value);
		}
		else
		{
			$self->index_text('text', $value);
		}
	}
}

1;
