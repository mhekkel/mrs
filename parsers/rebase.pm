package M6::Script::rebase;

use strict;
use warnings;

our @ISA = "M6::Script";

sub new
{
	my $invocant = shift;
	my $self = new M6::Script(
		headerline => qr/^CC.+/,
		lastdocline => '//',
		indices => {
			'id' => 'Identification',
			'ac' => 'Accession number',
			'com' => 'Commercial provider code (see REBASE commercial data)',
			'et' => 'Enzyme type',
			'os' => 'Organism species',
			'pt' => 'Prototype enzyme',
			'ref' => 'Any reference field',
			'target' => 'Target',
			'targetlen' => 'Target length',
		},
		@_
	);

	return bless $self, "M6::Script::rebase";
}

sub parse
{
	my ($self, $text) = @_;
	
	my ($osvalue);
	
	open(my $h, "<", \$text);
	while (my $line = <$h>)
	{
		chomp($line);

		if ($line =~ /^([A-Z]{2}) {3}/o)
		{
			my $fld = $1;
			my $value = substr($line, 5);
			
			if ($fld eq 'ID') {
				$self->set_attribute('id', $value);
# Guess what... id is not unique in REBASE... There
# are about 41 duplicate records in version 210..
#				$self->index_unique_string('id', $value);
				$self->index_string('id', $value);
			}
			elsif ($fld eq 'OS') {  # organism field
				$self->index_text('os', $value);
				$osvalue = $value;
			}
			elsif ($fld eq 'RS') {  # target line
				$self->set_attribute('title', "$osvalue target:$value");
				# since no DE line, use this as title
				$value =~ /([^,]+),.+;(([^,]+),.+;)?/;
				$self->index_string('target', lc($1));
				$self->index_number('targetlen', length($1)) if ($1 ne '?');
				if (defined $2)
				{
					$self->index_string('target', lc($3));
					$self->index_number('targetlen', length $3);
				}
			}
			elsif (substr($fld, 0, 1) eq 'C') { # commercial
				for my $letter ('A' .. 'Z') {
					if ($value =~ /$letter/) {
						$self->index_string('com', lc $letter)
					}
				}
			}
			elsif (substr($fld, 0, 1) eq 'R') { # reference, RA or RL
				$self->index_text('ref', $value);
			}
			elsif ($fld eq 'MS') {  # useless fields
			}
			else
			{
				$self->index_text(lc($fld), substr($line, 5));
			}
		}
	}
}

1;
		
