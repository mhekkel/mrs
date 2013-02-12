package M6::Script::prints;

use strict;
use warnings;

our @ISA = "M6::Script";

sub new
{
	my $invocant = shift;
	my $self = new M6::Script(
		firstdocline => qr/^gc; .+/,
		indices		=> {
			gd => 'Description',
		},
		@_
	);
	return bless $self, "M6::Script::prints";
}

sub parse
{
	my ($self, $text) = @_;
	
	my %ix = map { $_ => 1 } ( 'gc', 'gn', 'ga', 'gp', 'gr', 'gd', 'tp' );

	open(my $h, "<", \$text);
	while (my $line = <$h>)
	{
		chomp($line);

		my ($fld, $text) = split(m/; */, $line, 2);
		next unless defined $text and length($text);

		if ($fld eq 'st' or $fld eq 'tp')
		{
			foreach my $id (split(m/\s+/, $line))
			{
				$self->add_link('uniprot', $id);
			}
		}

		if ($fld eq 'gx')
		{
			$self->index_unique_string('id', $text);
			$self->set_attribute('id', $text);
		}
		elsif ($ix{$fld})
		{
			$self->set_attribute('title', $text) if $fld eq 'gc';
			$self->index_text($fld, $text);
		}
	}
}

1;
