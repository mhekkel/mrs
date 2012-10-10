# MRS plugin for creating an pfam db
#
# $Id: pfam.pm 18 2006-03-01 15:31:09Z hekkel $
#
# Copyright (c) 2005
#      CMBI, Radboud University Nijmegen. All rights reserved.
#
# This code is derived from software contributed by Maarten L. Hekkelman
# and Hekkelman Programmatuur b.v.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. All advertising materials mentioning features or use of this software
#    must display the following acknowledgement:
#        This product includes software developed by the Radboud University
#        Nijmegen and its contributors.
# 4. Neither the name of Radboud University nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE RADBOUD UNIVERSITY AND CONTRIBUTORS
# ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
# TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS
# BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
# 2007-02-23 some changes by Guy Bottu from BEN
#   long names for fields
#   nseq field
#   version subroutine

package MRS::Script::pfam;

our @ISA = "MRS::Script";

my %INDICES = (
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
);

my @links = (
	{
		match	=> qr|^(#=GF DR\s+PFAMA;\s)(\S+)(?=;)|mo,
		db		=> 'pfama',
		ix		=> 'ac'
	},
	{
		match	=> qr|^(#=GF DR\s+PFAMB;\s)(\S+)(?=;)|mo,
		db		=> 'pfamb',
		ix		=> 'ac'
	},
	{
		match	=> qr|^(#=GF DR\s+PDB;\s)(\S+)|mo,
		db		=> 'pdb',
		ix		=> 'id'
	},
	{
		match	=> qr|^(#=GF DR\s+PROSITE;\s)(\S+)(?=;)|mo,
		db		=> 'prosite_doc',
		ix		=> 'id'
	},
	{
		match	=> qr|^(#=GF DR\s+INTERPRO;\s)(\S+)(?=;)|mo,
		db		=> 'interpro',
		ix		=> 'id'
	},
	{
		match	=> qr|^(#=GS .+?AC )([0-9A-Z]+)|mo,
		db		=> 'uniprot',
		ix		=> 'ac'
	},
	{
		match	=> qr|^(#=GS .+DR PDB; )(\w{4})|mo,
		db		=> 'pdb',
		ix		=> 'id'
	},
);

sub new
{
	my $invocant = shift;
	
	my $self = new MRS::Script(
		url		=> 'http://pfam.sanger.ac.uk/',
		section	=> 'other',
		meta	=> [ 'title' ],
		indices	=> \%INDICES,
		@_
	);
	
	if (defined $self->{db}) {
		
		my %NAMES = (
			pfama		=> 'Pfam-A',
			pfamb		=> 'Pfam-B',
			pfamseed	=> 'Pfam-Seed'
		);

		$self->{name} = $NAMES{$self->{db}};
		
		my %FILES = (
			pfama		=> qr/Pfam-A\.full\.gz/,
			pfamb		=> qr/Pfam-B\.gz/,
			pfamseed	=> qr/Pfam-A\.seed\.gz/,
		);
		
		$self->{raw_dir} =~ s|pfam[^/]+/?$|pfam|;
		$self->{raw_files} = $FILES{$self->{db}};
	}
	
	return bless $self, "MRS::Script::pfam";
}

sub Parse
{
	my $self = shift;
	
	my ($doc, $state, $m);
	
	$state = 0;
	
	my $lookahead = $self->GetLine;
	while (defined $lookahead)
	{
		my $line = $lookahead;
		$lookahead = $self->GetLine;

		$doc .= $line;

		chomp($line);

		if ($line eq '//')
		{
			$self->Store($doc);
			$self->FlushDocument;

			$doc = undef;
		}
		elsif (substr($line, 0, 5) eq '#=GF ')
		{
			my $field = substr($line, 5, 2);
			my $value = substr($line, 10);

			if ($field eq 'ID')
			{
				$self->IndexValue('id', $value);
			}
			elsif ($field =~ /BM|GA|TC|NC/o)  # useless fields
			{}
			elsif ($field eq 'AC' and $value =~ m/(P[BF]\d+)/)
			{
				$value = $1;
				$self->IndexValue('ac', $value);
			}
			elsif ($field eq 'DR')
			{
				my @link = split(m/; */, $value);
				$self->IndexLink($link[0], $link[1]) if length($link[0]) > 0 and length($link[1]) > 0;
				
				$self->IndexText('ref', $value);
			}
			elsif (substr($field, 0, 1) eq 'R')
			{
				$self->IndexText('ref', $value);
			}
			elsif (substr($field, 0, 2) eq 'SQ')
			{
				$self->IndexNumber('nseq', $value);
			}
			else
			{
				$self->StoreMetaData('title', $value) if $field eq 'DE';
				$self->IndexText(lc($field), $value);
			}
		}
		elsif (substr($line, 0, 5) eq '#=GS ')
		{
			#		#=GS Q9ZNY5_SECCE/28-72   
			my $link = substr($line, 26);
			$self->IndexLink('uniprot', $1) if ($line =~ m/^AC (.+?)(\.\d)/);
			$self->IndexLink('pdb', $1) if ($line =~ m/^DR PDB; (\w{4})/);
			
			$self->IndexText('gs', substr($line, 5));
		}
	}
}

sub version
{
	my ($self) = @_;
	my $vers;

	my $raw_dir = $self->{raw_dir} or die "raw_dir is not defined\n";
	
	my $fh;

print "zcat $raw_dir/relnotes.txt.Z\n";
	open($fh, "zcat $raw_dir/relnotes.txt.Z|");

	while (my $line = <$fh>)
	{
		if ($line =~ /^\s+(RELEASE [0-9.]+)/)
		{
			$vers = $1;
			last;
		}
	}

	close($fh);

	chomp($vers);

	return $vers;
}

sub pp
{
	my ($this, $q, $text, $id, $url) = @_;
	
	$text = $this->link_url($text);

	# some entries are really way too large, so only when we have less than 1000 links:
	if ($text =~ m/^#=GF SQ\s+(\d+)/mo and int($1) < 1000)
	{
		foreach my $l (@links)
		{
			my $db = $l->{db};
			my $ix = $l->{ix};
			$text =~ s|$l->{match}|$1<mrs:link db='$db' index='$ix' id='$2'>$2</mrs:link>|g;
		}
	}
	
	return
		$q->div({-class=>'entry', 'xmlns:mrs' => 'http://mrs.cmbi.ru.nl/mrs-web/ml'},
		$q->pre($text));
}

1;
