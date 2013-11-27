package M6::Script::ptt;

our @ISA = "M6::Script";

sub new
{
	my $invocant = shift;
	my $self = new M6::Script(@_);
	return bless $self, "M6::Script::ptt";
}

sub parse
{
	my ($self, $text, $filename) = @_;

	print "entry parse $filename\n";

	die "invalid file: $filename\n" unless $filename =~ m|([^/]+).ptt$|;
	my $id = $1;
	$self->index_unique_string('id', $id);
	
	open(my $h, "<", \$text);
	my $doc = <$h>;
	my $title = $doc;
	chomp($title);

	$self->set_attribute('title', $title);
	$self->index_text('title', $title);
	
	while (my $l = <$h>)
	{
		next unless $l =~ m/^\d+\.\.\d+/;
		chomp($l);
		my @v = split(m/\t/, $l);
		my $cog = $v[7];

		$self->index_string('cog', $cog) unless $cog eq '-';
	}
	close($h);
}

1;
