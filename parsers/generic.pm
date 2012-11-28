package M6::Script::generic;

our @ISA = "M6::Script";

sub new
{
	my $invocant = shift;
	my $self = new M6::Script(@_);
	return bless $self, "M6::Script::generic";
}

sub parse
{
	my ($self, $text, $filename) = @_;

	# extremely simple... If filename is defined, use it as title,
	# otherwise we take the first line, truncate it to 255 characters
	# and make it the title. Finally index anything in the text.
	
	if (defined $filename)
	{
		$self->set_attribute('title', $filename);
		$self->set_attribute('filename', $filename);
	}
	elsif ($text =~ m/^.+/)
	{
		my $title = substr($&, 0, 255);
		$self->set_attribute('title', $title);
	}

	$self->index_text('text', $text);
}

1;
