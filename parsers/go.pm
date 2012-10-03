package M6::Script::go;

our @ISA = "M6::Script";

sub new
{
	my $invocant = shift;
	my $self = new M6::Script(
		firstdocline => qr/^\[Term\]/,
		trailer => qr/^\[Typedef\]/,
		@_
	);
	return bless $self, "M6::Script::go";
}

sub parse
{
	my ($self, $text) = @_;
	
	while ($text =~ m/^(\w+):\s+(.+)\n/mg)
	{
		my $name = $1;
		my $value = $2;

		if ($name eq 'id')
		{
			warn "invalid id value: $value" unless ($value =~ /GO:(\d+)/);
			$value = $1;
			$self->index_unique_string('id', $value);
		}
		else
		{
			$self->set_attribute('title', $value) if $name eq 'name';
			$name =~ tr/-a-zA-Z0-9/_/cs;
			$name = substr($name, 0, 15) if length($name) > 15;
			$self->index_text($name, $value);
		}
	}
}

1;
