package M6::Script::prosite;

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
			'cc' => 'Comments and Notes',
			'de' => 'Description',
			'do' => 'PROSITE documentation link',
			'dr' => 'Database cross-reference',
			'dt' => 'Date',
			'pp' => 'Post-Processing Rule',
			'pr' => 'Prorule link',
			'ru' => 'Rule',
			'type' => 'Type (PATTERN or MATRIX)'
		},
		@_
	);
	return bless $self, "M6::Script::prosite";
}

sub parse
{
	my ($self, $text) = @_;
	
	open(my $h, "<", \$text);
	while (my $line = <$h>)
	{
		chomp($line);

		if ($line =~ /^([A-Z]{2}) {3}/o)
		{
			my $fld = $1;
			my $value = substr($line, 5);

			if ($fld eq 'ID' and $value =~ /^([A-Z0-9_]+); ([A-Z]+)/o)
			{
				my $id = $1;
				die "ID too short" unless length($id);
				$self->set_attribute('id', $id);
				$self->index_unique_string('id', $id);
				$self->index_string('type', lc $2);
			}
			elsif ($fld =~ /MA|NR|PA/o)  # useless fields
			{}
			elsif ($fld eq 'DR')
			{
				while ($value =~ m/\S+?, (\S+)\s*, \S;/g)
				{
					$self->add_link('uniprot', $1);
				}
				
#				my @l = split(m/;\s*/, $value);
#				foreach my $l (@l)
#				{
#					$self->add_link('uniprot', $1)
#						if ($l =~ m/^(\w+),/);
#				}
				
				$self->index_text(lc $fld, $value);
			}
			elsif ($fld eq '3D')
			{
				my @l = split(m/;\s*/, $value);
				foreach my $l (@l)
				{
					$self->add_link('pdb', $l);
				}
				
				$self->index_text(lc $fld, $value);
			}
			elsif ($fld eq 'DO')
			{
				$self->add_link('prosite_doc', $1)
					if ($value =~ m/(PDOC\d+)/);
				$self->index_text(lc $fld, $value);
			}
			else
			{
				$self->set_attribute('title', $value) if $fld eq 'DE';
				$self->index_text(lc($fld), $value);
			}
		}
	}
}

1;
