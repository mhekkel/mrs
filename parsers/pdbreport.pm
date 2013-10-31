package M6::Script::pdbreport;

use strict;
use warnings;

our @ISA = "M6::Script";

sub new
{
	my $invocant = shift;
	my $self = new M6::Script(
		indices => {
			id		=> 'PDB Identifier',
			error		=> 'Reported Error',
			warning		=> 'Given Warning',
			uubb		=> 'Number of residues with unusual backbone torsion angles',
			uuro		=> 'Number of residues with unusual rotamers',
			buhd		=> 'Number of buried unsatisfied hydrogen bond donor residues',
			buha		=> 'Number of buried unsatisfied hydrogen bond acceptor residues',
			asid		=> 'Number of abnormally short interatomic distances',
			uuba		=> 'Number of unusual bond angles',
			uuta		=> 'Number of residues with unusual torsion angles',
			missingatoms	=> 'Number of missing atoms',
			abpe		=> 'Number of residues with abnormal packing environment'
		},
		@_
	);
	return bless $self, "M6::Script::pdbreport";
}

sub parse
{
	my ($self, $text) = @_;

	my %header2varid = (
		'Backbone evaluation reveals unusual conformations'	=> 'uubb',
		'Buried unsatisfied hydrogen bond donors'		=> 'buhd',
		'Buried unsatisfied hydrogen bond acceptors'		=> 'buha',
		'Abnormally short interatomic distances'		=> 'asid',
		'Unusual bond angles'					=> 'uuba',
		'Missing atoms'						=> 'missingatoms',
		'Torsion angle evaluation shows unusual residues'	=> 'uuta',
		'Abnormal packing environment for some residues'	=> 'abpe',
		'Unusual rotamers'					=> 'uuro'
	);

	my $header='';
	my $type='';

	my %counts = ( uubb => 0, buhd => 0, buha => 0, asid => 0, uuba => 0, uuta => 0, abpe => 0, missingatoms => 0, uuro => 0 );

	open(my $h, "<", \$text);
	while (my $line = <$h>)
	{
		if ( $line eq '==============' ) # indicates the end of the file
		{
			last;
		}

		if ( $line =~ m/^==== Compound code pdb(\d[a-z\d]{3})\.ent\s+====$/ )
		{
			my $id = $1;

			$self->index_unique_string('id', $id);
			$self->set_attribute('id', $id);

			$self->add_link('pdb', $id);
			$self->add_link('dssp', $id);
			$self->add_link('hssp', $id);
			$self->add_link('pdbfinder2', $id);
			$self->add_link('pdb_redo', $id);
			
		}
		elsif ( $line =~ m/^# (\d+) # ([A-Z][a-z]+): (.*)$/ )
		{
			my $sectionnumber = $1;
			$type = lc $2;
			$header = $3;

			if ( $type eq 'error' or $type eq 'warning' )
			{
				$self->index_text( $type, $header );
			}
		}
		elsif ( $line =~ m/\d+\s+[A-Z]{3}\s+\(\s*\d+\-\)\s+[A-Za-z0-9]/ )
		{
			# This line represents a residue

			if (exists $header2varid{$header} )
			{
				$counts{ $header2varid{$header} } ++;
			}
		}
		elsif ( $line =~ m/And so on for a total of\s+(\d+) lines./ )
		{
			# This line represents multiple residues

			if (exists $header2varid{$header} )
			{
				$counts{ $header2varid{$header} } += $1;
			}
		}
	}

	foreach my $key (keys %counts)
	{
		$self->index_number( $key , $counts{ $key } );
	}
}

1;
