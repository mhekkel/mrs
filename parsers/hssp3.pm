package M6::Script::hssp3;

our @ISA = "M6::Script";

sub new
{
	my $invocant = shift;
	my $self = new M6::Script(@_);
	return bless $self, "M6::Script::hssp3";
}

sub parse
{
	my ($self, $text, $filename) = @_;

	if ($filename =~ m/([0-9][0-9a-z]{3})(\.hssp(\.bz2)?)?/i)
	{
		$self->set_attribute('id', $1);
		$self->index_unique_string('id', $1);
	}
}

1;
