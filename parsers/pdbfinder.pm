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

sub to_fasta
{
	my ($self, $text, $db, $id, $title) = @_;

	open(my $h, "<", \$text);
	my ($state, $id, $chainid, %seq);
	
	$state = 0;

	while (my $line = <$h>)
	{
		chomp($line);
		
		last if (substr($line, 0, 2) eq '//');
		
		if ($line =~ /^Chain\s*:\s*(\S)/)
		{
			$chainid = $1;
		}
		elsif ($line =~ /^\s*Sequence\s*:\s*(\w+)/)
		{
			my $seq = uc $1;
			$seq =~ tr/ARNDCQEGHILKMFPSTWYVBZX//dc;
			$seq{$chainid} = $seq;
		}
	}

	my $result = '';
	if (scalar keys %seq)
	{
		foreach my $chain (keys %seq)
		{
			my $seq = $seq{$chain};
			$result .= ">gnl|$db|$id|$chain $title\n$seq\n";
		}
	}
	
	return $result;
}

1;
