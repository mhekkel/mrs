package M6::Script::pdbfinder;

our @ISA = "M6::Script";

sub new
{
	my $invocant = shift;
	my $self = new M6::Script(
		header => qr'^//.*',
		lastdocline => '//',
		@_
	);
	return bless $self, "M6::Script::pdbfinder";
}

sub parse
{
	my ($self, $text) = @_;
	
	while ($text =~ m/^\s?(\w+)\s+:\s(.+)\n/mg)
	{
		my $key = lc $1;
		my $value = $2;
		
		if ($key eq 'id')
		{
			$value =~ s/\s+$//;
			$self->index_unique_string('id', $value);
			$self->set_attribute('id', $value);
		}
		elsif ($key eq 'header')
		{
			$self->index_text('header', $value);
			$self->set_attribute('title', lc $value);
		}
		elsif ($key eq 'author')
		{
			$value =~ s/\./. /g;
			$self->index_text('author', $value);
		}
		elsif ($key eq 'chain')
		{
			last;
		}
		else
		{
			$self->index_text('text', $value);
		}
	}
}

1;
