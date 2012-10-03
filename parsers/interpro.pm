package M6::Script::interpro;

our @ISA = "M6::Script";

sub new
{
	my $invocant = shift;
	my $self = new M6::Script(
		firstdocline => qr/^<interpro\s.*/,
		lastdocline => '</interpro>',
		@_
	);

	return bless $self, "M6::Script::interpro";
}

sub parse
{
	my ($self, $text) = @_;

	if ($text =~ m/<interpro ([^>]+)>/)
	{
		my $i = $1;
		if ($i =~ m/id="(.+?)"/)
		{
			$self->index_unique_string('id', $1);
			$self->set_attribute('id', $1);
		}
		$self->index_string('type', $1) if ($i =~ m/type="(.+?)"/);
		$self->index_string('name', $1) if ($i =~ m/short_name="(.+?)"/);
	}

	$self->set_attribute('title', $1) if ($text =~ m|<name>(.+?)</name>|);

	# index all attributes
	while ($text =~ m'<([^-> ]+)\s([^>/]+)/?>'gm)
	{
		my $name = $1;
		my $attr = $2;
		
		if ($name eq 'db_xref')
		{
			my $db = $1 if $attr =~ m/db="(.+?)"/;
			my $id = $1 if $attr =~ m/dbkey="(.+?)"/;
			$self->add_link($db, $id) if defined $db and defined $id;
		}
		
		while ($attr =~ m/\w+="(.+?)"/gm)
		{
			$self->index_text('text', $1);
		}
	}

	# and all text that remains after stripping out the tags
	$text =~ s/<[^>]*>/ /g;
	
	$self->index_text('text', $text);
}

1;
