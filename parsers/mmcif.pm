package M6::Script::mmcif;

use strict;
use warnings;

our @ISA = "M6::Script";

our %aa_map = (
	'ALA'	=>	'A',
	'ARG'	=>	'R',
	'ASN'	=>	'N',
	'ASP'	=>	'D',
	'CYS'	=>	'C',
	'GLN'	=>	'Q',
	'GLU'	=>	'E',
	'GLY'	=>	'G',
	'HIS'	=>	'H',
	'ILE'	=>	'I',
	'LEU'	=>	'L',
	'LYS'	=>	'K',
	'MET'	=>	'M',
	'PHE'	=>	'F',
	'PRO'	=>	'P',
	'SER'	=>	'S',
	'THR'	=>	'T',
	'TRP'	=>	'W',
	'TYR'	=>	'Y',
	'VAL'	=>	'V',
	'GLX'	=>	'Z',
	'ASX'	=>	'B',
);

sub new
{
	my $invocant = shift;
	my $self = new M6::Script(@_);
	return bless $self, "M6::Script::mmcif";
}

sub parse
{
	my ($self, $text) = @_;

	my ($header, $title, $compound, %atoms_per_model, %ligands);

	my $data=&parse_mmcif( $text );

	if (defined $data->{'_struct_keywords'} and (scalar @{$data->{'_struct_keywords'}})==1 )
	{
		if (defined $data->{'_struct_keywords'}->[0]->{'pdbx_keywords'})
		{
			$header = $data->{'_struct_keywords'}->[0]->{'pdbx_keywords'};
			$self->index_text('text', $header);
		}
		if (defined $data->{'_struct_keywords'}->[0]->{'entry_id'})
		{
			my $id=$data->{'_struct_keywords'}->[0]->{'entry_id'};
			$self->set_attribute('id', $id );
			$self->index_unique_string('id', $id );
			$self->add_link('dssp', $id);
			$self->add_link('hssp', $id);
			$self->add_link('pdbfinder2', $id);
		}
	}
	elsif ( defined $data->{'_entry'} and (scalar @{$data->{'_entry'}})==1 and defined $data->{'_entry'}->[0]->{'id'} )
	{
		my $id=$data->{'_entry'}->[0]->{'id'};
		$self->set_attribute('id', $id );
		$self->index_unique_string('id', $id );
		$self->add_link('dssp', $id);
		$self->add_link('hssp', $id);
		$self->add_link('pdbfinder2', $id);
	}

	foreach my $atom ( @{ $data->{'_atom_site'} } )
	{
		my $atomtype = $atom->{ 'group_PDB' };
		my $modelnum = $atom->{ 'pdbx_PDB_model_num' };
		my $compid = $atom->{ 'auth_comp_id' };

		$atoms_per_model{$modelnum}=0 if not defined $atoms_per_model{$modelnum};
		$atoms_per_model{$modelnum}++;

		if($atomtype eq 'HETATM')
		{
                        $ligands{$compid} = 1;
		}
	}
	my $nmodels=scalar (keys %atoms_per_model);
	$self->index_number('models', $nmodels) if $nmodels>0;

	if(defined $data->{'_struct'} and (scalar @{$data->{'_struct'}})==1 and $data->{'_struct'}->[0]->{'title'})
	{
		$title .= lc $data->{'_struct'}->[0]->{'title'};
		$self->index_text('title', $data->{'_struct'}->[0]->{'title'});
	}

	for my $entity (@{$data->{'_entity'}})
	{
		my ($id,$type,$description,$ec,$mutation,$fragment,$details) = (	$entity->{'id'},
											$entity->{'type'},
											$entity->{'pdbx_description'},
											$entity->{'pdbx_ec'},
											$entity->{'pdbx_mutation'},
											$entity->{'pdbx_fragment'},
											$entity->{'details'} );
		next if $type ne 'polymer';

		if ($description and $description ne '?')
		{
			$self->index_text('compnd',$description);
			$compound .= lc $description . ' ';
		}
		if($ec and $ec ne '?')
		{
			$self->index_text('compound',$ec);
			foreach my $ecc (split(m/, /, $ec)) {
				$self->add_link('enzyme', $ecc);
			}
			$compound .= 'EC: ' . lc $ec . ' ';
		}
		$self->index_text('compnd',$mutation) if $mutation and $mutation ne '?';
		$self->index_text('compnd',$fragment) if $fragment and $fragment ne '?';
		$self->index_text('compnd',$details ) if $details  and $details  ne '?';
	}
	for my $entity (@{$data->{'_entity_name_com'}})
	{
		my ($id,$name) = ($entity->{'entity_id'},$entity->{'name'});
		$self->index_text('compnd',$name);
	}
	for my $entity (@{$data->{'_entity_poly'}})
	{
#		my $id = $entity->{'entity_id'};
#		my $type = $entity->{'type'};
#		my $strand = $entity->{'pdbx_strand_id'};

		$self->index_text('compnd',$entity->{'type'}) if defined $entity->{'type'};
	}
	for my $author (@{$data->{'_audit_author'}})
	{
		$self->index_text('ref', $author->{'name'});
	}
	for my $cit (@{$data->{'_citation'}})
	{
		for my $key (keys %{ $cit })
		{
			next if $key eq 'id' or $cit->{$key} eq '?';

			$self->index_text('ref', $cit->{$key});
		}
	}

	# Database references
	for my $ref ( @{ $data->{'_struct_ref'} })
	{
		my $db = $ref->{'db_name'};
		my $code = $ref->{'db_code'};
		my $ac = $ref->{'pdbx_db_accession'};

		my %dbmap = (
	                embl    => 'embl',
        	        gb      => 'genbank',
			ndb     => 'ndb',
	                pdb     => 'pdb',
	                pir     => 'pir',
	                prf     => 'profile',
	                sws     => 'uniprot',
	                trembl  => 'uniprot',
	                unp     => 'uniprot'
		);
		$db = $dbmap{(lc $db)} if defined $dbmap{(lc $db)};

		if ( (length $db) > 0)
		{
			$self->add_link($db, $code)	if length($code);
			$self->add_link($db, $ac)	if length($ac);
		}
	}

	$header = "$title ($header)" if (defined $title and (length $title) > 0);
	$header .= "; $compound" if (defined $compound and (length $compound) > 0);
	$header = substr($header, 0, 255) if ( (length $header) > 255);
	$header =~ s/ {2,}/ /g;
	$self->set_attribute('title', $header);

	foreach my $ligand (keys %ligands)
	{
		$ligand =~ s/^\s*(\S+)\s*$/$1/;
		$self->index_string("ligand", $ligand);
	}
}

sub unquote
{
	my $str = $_[0];

	if ($str =~ m/^\"(.*)\"$/ ) {
	    $str =~ s/^\"(.*)\"$/$1/;
	} elsif ($str =~ m/^\'(.*)\'$/ ) {
		$str =~ s/^\'(.*)\'$/$1/;
	}

    return $str;
}

sub parse_values_line
{
	my $line = $_[0];

	my @words=split /\s+/,$line;
	my $nwords=scalar @words;

	my @values=();
	my $i=0;
	while ($i<$nwords)
	{
		if ( $words[$i] =~ m/^[\'\"]/ ) # starts with a quote
		{
			my $value = '';
			my $quote = substr $words[$i],0,1;

			until ( (length $value)>1 and (substr $value,-1) eq $quote ) # must end in the same quote
			{
				$value .= ' ' if $value;
				$value .= $words[$i];

				$i++;
			}

			push @values, &unquote($value);
		}
		else
		{
			push @values, &unquote($words[$i]) if (length $words[$i])>0;
			$i++;
		}
	}

	return @values;
}

sub parse_mmcif
{
	my $text = $_[0];

	open(my $h, "<", \$text);

	my $categories={};

	my ($catid,$loop)=('',0);
	my @values=();
	my @varids=();
	while (my $line = <$h> )
	{
		chomp $line;

		if ($line =~ m/^(data_|#)/)
		{
			my ($nvar,$nval)=(scalar @varids,scalar @values);
			if ($nval>0 and $nval<$nvar)
			{
				my $val='[' . (join ',',@values) . ']';
				die "Too few values (length of $val < $nvar) parsed for $catid\n";
			}

			$loop=0;
			(@values,@varids)=((),());
		}
		elsif ($line =~ m/^loop_$/ )
		{
			$loop=1;
		}
		elsif ($line =~ m/^_/ )
		{
			my ($varid,$value);

			die "Syntax error for variable ID on line \"$line\"\n"
				unless ($line =~ m/([^\s]+)\.([^\s]+)(\s+[^\'\"\s]+|\s+\".+\"|\s+\'.+\'|)/ );

			($catid,$varid,$value) = ( $1, $2, $3 );
			$value =~ s/^\s+//;

			$categories->{$catid}=[] if not defined $categories->{$catid};
			
			if ($loop)
			{
				push @varids,$varid;
			}
			else # No loop, expecting only one row for this category
			{
				if( (scalar @{$categories->{$catid}}) ne 1)
				{
					$categories->{$catid} = [ {} ];
				}

				if( (length $value)==0 ) # expect the value to be on the next line:
				{
					$line = <$h>;
					chomp $line;
					if ( $line =~ m/^;/ )
					{
						$value = substr($line,1);
						while (1)
						{
							$line = <$h>;
							last if ( $line =~ m/^;/ );

							chomp $line;
							$value .= $line;
						}
					}
					else
					{
						$value=$line;
					}
				}

				$categories->{$catid}->[0]->{$varid}=&unquote($value);
			}
		}
		elsif ($loop)
		{
			if ( $line =~ m/^;/ )
			{
				my $i = scalar @values;
				push @values, substr($line,1);

				while (1)
				{
					$line = <$h>;
					last if ( $line =~ m/^;/ );

					chomp $line;
					$values[$i].=$line;
				}
			}
			else
			{
				@values = ( @values, &parse_values_line( $line ) );
			}

			my ($nvar,$nval)=(scalar @varids,scalar @values);
			if ( $nvar == $nval )
			{
				my $row={};
				for(my $i=0; $i<$nvar; $i++ )
				{
					my $value=$values[ $i ];
					$value =~ s/^\"(.*)\"$/$1/;
					$row->{ $varids[$i] }=$value;
				}
				push @{ $categories->{$catid} }, $row;
				@values=();
			}
			elsif ( $nval > $nvar )
			{
				my $val='[' . (join ' , ',@values) . ']';
				die "Too many values in $catid: (length of $val > $nvar)\n";
			}
		}
	}

	return $categories;
}

sub to_fasta
{
	my ($self, $text, $db, $id, $title) = @_;

	my %seq;

	my $data=&parse_mmcif( $text );
	my $scheme=$data->{'_pdbx_poly_seq_scheme'};

	foreach my $row (@{$scheme})
	{
		my $chainId=$row->{'asym_id'};
		my $res=$row->{'mon_id'};

		next unless (length($res) == 3);
		my $aa = $aa_map{$res};
		$aa = 'X' unless defined $aa;

		$seq{$chainId} = '' unless defined $seq{$chainId};
		$seq{$chainId} .= $aa;
	}

	my $result = '';
	$title = '' unless $title;
	
	foreach my $chainId (keys %seq)
	{
		my $s = $seq{$chainId};
		next unless length($s) > 0;

		$s =~ s/(.{72})/$&\n/g;
		$result .= ">gnl|$db|$id|$chainId $title\n$s\n";
	}
	
	return $result;
}

#if( (scalar @ARGV) eq 1)
#{
#	open FILE, $ARGV[0] or die "Couldn't open file: $ARGV[0]";
#	my $string='';
#	while (my $line=<FILE>)
#	{
#		$string .= $line;
#	}
#	close FILE;
#	my $data=&parse_mmcif( $string );
#}

1;
