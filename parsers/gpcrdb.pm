# MRS plugin for creating a db
#
# $Id: uniseq.pm 169 2006-11-10 08:02:05Z hekkel $
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

package MRS::Script::gpcrdb;

use XML::Simple;
use XML::Compile::WSDL11;
use XML::Compile::SOAP11;
use XML::Compile::Transport::SOAPHTTP;

use Data::Dumper;

our @ISA = "MRS::Script";

sub new
{
	my $invocant = shift;
	my $self = new MRS::Script(
		section		=> 'protein',
		meta		=> [ 'title' ],
		raw_files	=> qr/protein_ids\.txt/,
		blast		=> 'mrs',
		seqtype		=> 'P',
		@_
	);

	if ($self->{db} eq 'gpcrdb-new')
	{
		$self->{url} => 'http://www.gpcr.org/7tm';
		$self->{name} = 'gpcrdb-new';
	}
	elsif ($self->{db} eq 'nucleardb')
	{
		$self->{url} => 'http://www.receptors.org/';
		$self->{name} = 'nucleardb';
	}
	
	return bless $self, "MRS::Script::gpcrdb";
}

sub Parse
{
	my $self = shift;
	
	my ($doc, $m, $seq);
	
	# never mind what's in the getProteinCount file...
	
	# Retrieving and processing the WSDL
	my $ws_url = 'http://cmbi23.cmbi.ru.nl:8080/mcsis-web/webservice/';
	$ws_url = 'http://cmbi23.cmbi.ru.nl:8080/mcsis-web-nrdb/webservice/'
		if ($self->{db} eq 'nucleardb');
	
	my $wsdl  = XML::LibXML->new->parse_file("$ws_url/?wsdl");
	my $trans = XML::Compile::Transport::SOAPHTTP->new(address => $ws_url);
	my $proxy = XML::Compile::WSDL11->new($wsdl); # , transport => $trans);

	my $getProtein = $proxy->compileClient('getProtein', address => $ws_url);
	
	while (my $proteinId = $self->GetLine)
	{
		chomp($proteinId);
		
		my ($answer, $trace) = $getProtein->(proteinId => $proteinId);
		die "geen eiwit" unless defined $answer;
		
		my $protein = $answer->{parameters}->{protein};
		
		$self->Store(XMLout($protein));
		$self->AddSequence($protein->{sequence});
		$self->IndexValue('id', $proteinId);

		$self->IndexValue('ac', $protein->{ac})
			if defined $protein->{ac} and length($protein->{ac});
		$self->IndexTextAndNumbers('upi', $protein->{upi})
			if defined $protein->{upi};
		$self->IndexTextAndNumbers('crc64', $protein->{crc64})
			if defined $protein->{crc64};

		my $title;
		foreach my $desc (@{$protein->{descriptions}})
		{
			if (defined $desc->{description})
			{
				$self->IndexTextAndNumbers('description', $desc->{description});
				$title .= "; " if defined($title);
				$title .= $desc->{description};
			}
		}

		$self->StoreMetaData('title', $title) if (length($title));

		foreach my $gene (@{$protein->{genes}})
		{
			$self->IndexText('gene', $gene->{gene})
				if defined $gene->{gene};
		}
		
		$self->IndexNumber('species-id', $protein->{species}->{id})
			if defined $protein->{species}->{id};
		$self->IndexTextAndNumbers('species', $protein->{species}->{scientificName})
			if defined $protein->{species}->{scientificName};
		
		$self->FlushDocument();
	}
}

sub pp
{
	my ($this, $q, $text, $id, $url) = @_;
	
	my $xml = XMLin($text);
	my $id = $xml->{id};
	
	my $url = "http://www.gpcr.org/7tm/proteins/$id";
	$url = "http://www.receptors.org/nucleardb/proteins/$id" if $this->{db} eq 'nucleardb';
	
	return sprintf(
		"<iframe width='100%' height='800px' src='%s'><a href='%s'>%s</a></iframe>",
		$url, $url, $url);
}

1;
