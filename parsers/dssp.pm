package M6::Script::dssp;

our @ISA = "M6::Script";

sub new
{
	my $invocant = shift;
	my $self = new M6::Script(@_);
	return bless $self, "M6::Script::dssp";
}

sub parse
{
	my ($self, $text) = @_;

	my $title;

	while ($text =~ m/^.+$/mg)
	{
		my $line = $&;

		last if (substr($line, 0, 1) eq ' ');
		
		if ($line =~ /^HEADER(.+?)\d\d-[A-Z]{3}-\d\d\s+(.{4})/o)
		{
			$title = $1;
			$title =~ s/\s+$//;

			$self->set_attribute('id', $2);
		}
		elsif ($line =~ /^COMPND.*?(?:MOLECULE: )?(?: |\d )(.+)/mo)
		{
			my $cmp = $1;
			$cmp =~ s/\s+;?\s*$//;
			$title .= '; ' . lc($cmp);
		}
		elsif ($line =~ /^AUTHOR\s+(.+)/o)
		{
			my $author = $1;
			$author =~ s/(\w)\.(?=\w)/$1. /og;
			$self->index_text('ref', $author);
		}
		else
		{
			$self->index_text('text', $line);
		}
	}
	
	$self->set_attribute('title', $title);
}

1;
