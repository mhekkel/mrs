package M6::Script::fasta;

our @ISA = "M6::Script";

sub new
{
	my $invocant = shift;
	my $self = new M6::Script(
		firstdocline => qr/^>.+/,
		@_
	);
	return bless $self, "M6::Script::fasta";
}

sub parse
{
	my ($self, $text) = @_;
	
	my ($id, $comment) = ($1, $2) if $text =~ m/^>(\S+)(?: (.*))?/;
	die "no id" unless defined $id;
	
	$self->set_attribute('title', $comment);
	$self->index_text('title', $comment);
	
	if ($id =~ m/^(?:lcl|gi|bbs|bbm|gim)\|([^|]*)$/)
	{
		$self->set_attribute('id', $1);
		$self->index_unique_string('id', $1);
	}
	elsif ($id =~ m/^(?:gb|emb|pir|sp|ref|gnl|dbj|prf|tpg|tpe|tpd|tr|gpp|nat)\|([^|]*)\|([^|]*)$/)
	{
		$self->index_unique_string('acc', $1);
		$self->set_attribute('id', $2);
		$self->index_unique_string('id', $2);
	}
	elsif ($id =~ m/^pat\|([^|]*)\|([^|]*)\|([^|]*)$/)
	{
		$self->set_attribute('id', $id);
		$self->index_unique_string('id', $id);

		$self->index_string('country', $1);
		$self->index_string('patent', $2);
		$self->index_string('sequence', $3);
	}
	elsif ($id =~ m/^pgp\|([^|]*)\|([^|]*)\|([^|]*)$/)
	{
		$self->set_attribute('id', $id);
		$self->index_unique_string('id', $id);

		$self->index_string('country', $1);
		$self->index_string('application', $2);
		$self->index_string('sequence', $3);
	}
	elsif ($id =~ m/^pdb\|([^|]*)\|([^|]*)$/)
	{
		$self->set_attribute('id', "$1$2");
		$self->index_unique_string('id', "$1.$2");

		$self->index_string('entry', $1);
		$self->index_string('chain', $2);
	}
	else
	{
		$self->set_attribute('id', $id);
		$self->index_unique_string('id', $id);
	}
}

1;
