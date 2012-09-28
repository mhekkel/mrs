package M6::Script::hssp;

our @ISA = "M6::Script";

sub new
{
	my $invocant = shift;
	my $self = new M6::Script(@_);
	return bless $self, "M6::Script::hssp";
}

sub parse
{
	my ($self, $text) = @_;

	while ($text =~ m/^.+$/mg)
	{
		my $line = $&;

		last if ($line =~ /^## ALIGNMENTS/);
		
		if ($line =~ /^PDBID\s+(\S+)/)
		{
			$self->index_unique_string('id', $1);
			$self->set_attribute('id', $1);
		}
		elsif ($line =~ /^(\S+?)\s+(.*)/)
		{
			my $fld = $1;
			$line = $2;
			
			$line =~ s/(\w)\.(?=\w)/$1. /og
				if ($fld eq 'AUTHOR');

			$self->set_attribute('title', lc($line))
				if ($fld eq 'HEADER');

			my %fldmap = (
				'NALIGN'		=> 1,
				'SEQLENGTH'		=> 1,
				'NCHAIN'		=> 1,
				'KCHAIN'		=> 1
			);

			if (defined $fldmap{$fld}) {
				my $value = $1 if ($line =~ m/^(\d+)/);
				$self->index_number(lc $fld, $value);
			}
			else {
				$self->index_text('text', $line);
			}
		}
	}
}

1;
