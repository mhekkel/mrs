package M6::Script::pfam;

our @ISA = "M6::Script";

sub new
{
	my $invocant = shift;
	
	my $self = new M6::Script(
		firstdocline => '# STOCKHOLM 1.0',
		lastdocline => '//',
		indices => {
			'id' => 'Identification',
			'ac' => 'Accession number',
			'au' => 'Entry author',
			'am' => 'Alignment method (localfirst, globalfirst or byscore)',
			'cc' => 'Comments and Notes',
			'dc' => 'Comment about database reference',
			'de' => 'Description',
			'dr' => 'Database cross-reference',
			'gs' => 'UniProt sequence cross-reference',
			'ne' => 'Pfam accession number nested domain',
			'nseq' => 'The number of sequences per cluster',
			'nl' => 'Location nested domain',
			'pi' => 'Previous identifier',
			'ref' => 'Any reference field',
			'se' => 'Source of seed',
			'sq' => 'Number of sequences in alignment',
			'tp' => 'Type (Family, Domain, Motif or Repeat)',
		},
		@_
	);
	
	return bless $self, "M6::Script::pfam";
}

sub parse
{
	my ($self, $text) = @_;
	
	open(my $h, "<", \$text);
	while (my $line = <$h>)
	{
		chomp($line);

		if (substr($line, 0, 5) eq '#=GF ')
		{
			my $field = substr($line, 5, 2);
			my $value = substr($line, 10);

			if ($field eq 'ID')
			{
				$self->index_unique_string('id', $value);
				$self->set_attribute('id', $value);
			}
			elsif ($field =~ /BM|GA|TC|NC/o)  # useless fields
			{}
			elsif ($field eq 'AC' and $value =~ m/(P[BF]\d+)/)
			{
				$value = $1;
				$self->index_string('ac', $value);
			}
			elsif ($field eq 'DR')
			{
				my @link = split(m/; */, $value);
				$self->add_link($link[0], $link[1]) if length($link[0]) > 0 and length($link[1]) > 0;
				
				$self->index_text('ref', $value);
			}
			elsif (substr($field, 0, 1) eq 'R')
			{
				$self->index_text('ref', $value);
			}
			elsif (substr($field, 0, 2) eq 'SQ')
			{
				$self->index_number('nseq', $value);
			}
			else
			{
				$self->set_attribute('title', $value) if $field eq 'DE';
				$self->index_text(lc($field), $value);
			}
		}
		elsif (substr($line, 0, 5) eq '#=GS ')
		{
			#		#=GS Q9ZNY5_SECCE/28-72   
			my $link = substr($line, 26);
			$self->add_link('uniprot', $1) if ($line =~ m/^AC (.+?)(\.\d)/);
			$self->add_link('pdb', $1) if ($line =~ m/^DR PDB; (\w{4})/);
			
			$self->index_text('gs', substr($line, 5));
		}
	}
}

#sub version
#{
#	my ($self, $config) = @_;
#	my $vers;
#
#print STDERR "'$config'\n";
#
#	my $raw_dir = $self->{raw_dir} or die "raw_dir is not defined\n";
#	
#	my $fh;
#
#	open($fh, "zcat $raw_dir/relnotes.txt.Z|");
#
#	while (my $line = <$fh>)
#	{
#		if ($line =~ /^\s+(RELEASE [0-9.]+)/)
#		{
#			$vers = $1;
#			last;
#		}
#	}
#
#	close($fh);
#
#	chomp($vers);
#
#	return $vers;
#}

1;
