package M6::Script::taxonomy;

our @ISA = "M6::Script";

sub new
{
	my $invocant = shift;
	my $self = new M6::Script(
		lastdocline => '//',
		@_
	);
	return bless $self, "M6::Script::taxonomy";
}

sub parse
{
	my ($self, $text) = @_;

	while ($text =~ m/^([A-Z][^:]+?) +: (.+)/mg)
	{
		my $key = $1;
		my $value = $2;
		
		if ($key eq 'ID')
		{
			$self->index_unique_string('id', $value);
			$self->set_attribute('id', $value);
		}
		elsif ($key eq 'SCIENTIFIC NAME')
		{
			$self->index_string('sn', $value);
			$self->index_text('text', $value);
			$self->set_attribute('title', $value);
		}
		else
		{
			$self->index_text('text', $value);
		}
	}
}

1;
