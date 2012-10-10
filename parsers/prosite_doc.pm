package M6::Script::prosite_doc;

our @ISA = "M6::Script";

sub new
{
	my $invocant = shift;
	my $self = new M6::Script(
		lastdocline => '{END}',
		@_
	);
	
	return bless $self, "M6::Script::prosite_doc";
}

sub parse
{
	my ($self, $text) = @_;
	
	open(my $h, "<", \$text);
	my $title;
	
	while (my $line = <$h>)
	{
		chomp($line);

		if ($line =~ /^\{(PDOC\d+)\}/o)
		{
			$self->set_attribute('id', $1);
			$self->index_unique_string('id', $1);
		}
		elsif (not defined $title and $line =~ m/\*\s*([^*].+?)\s*\*/) {
			$self->set_attribute('title', $1);
			$self->index_text('text', $1);
			$title = $1;
		}
		else
		{
			$self->index_text('text', $line);
		}
	}
}

1;
