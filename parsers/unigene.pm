package M6::Script::unigene;

use strict;
use warnings;

our @ISA = "M6::Script";

sub new
{
	my $invocant = shift;
	my $self = new M6::Script(
		lastdocline => '//',
		@_
	);
	return bless $self, "M6::Script::unigene";
}

sub parse
{
	my ($self, $text) = @_;
	
	open(my $h, "<", \$text);
	while (my $line = <$h>)
	{
		chomp($line);

		if ($line =~ /^(\S+)\s+(.+?)\s*$/o)
		{
			my $fld = $1;
			my $value = $2;

			if ($fld eq 'ID' and $value =~ /(^\S+)/)
			{
				my $id = $1;
				$self->index_unique_string('id', $id);
				$self->set_attribute('id', $id);
			}
			else
			{
				$self->set_attribute('title', substr($value, 0, 255))
					if $fld eq 'TITLE';

				$self->index_text(lc($fld), $value);
			}
		}
	}
}

1;
