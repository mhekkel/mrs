package M6::Script::oxford;

use strict;
use warnings;

our @ISA = "M6::Script";

sub new
{
	my $invocant = shift;
	my $self = new M6::Script(
		lastheaderline => qr/^-[- ]+$/,
		firstdocline => qr/^\d+.+/,
		indices => {
			'id'		=> 'Unique ID',
			'llhid'		=> 'Human EntrezGene ID',
			'hsym'		=> 'Human Symbol',
			'loc'		=> 'Human Chr',
			'acc'		=> 'Mouse MGI Acc ID',
			'llmid'		=> 'Mouse EntrezGene ID',
			'msym'		=> 'Mouse Symbol',
			'chr'		=> 'Mouse Chr',
			'cm'		=> 'Mouse cM',
			'cb'		=> 'Mouse Band',
			'data'		=> 'Data Attributes',
			'loc_min'	=> 'Cytoloc number min',
			'loc_max'	=> 'Cytoloc number max'
		},
		@_
	);
	return bless $self, "M6::Script::oxford";
}

sub parse
{
	my ($self, $text, $filename, $header) = @_;

	unless (defined $self->{offsets})
	{
		my $line = $& if $header =~ m/^-+[- ]+$/mg;
		
		my $offset = 0;
		my @offsets = ( );
		
		while ($line =~ m/-+\s*/g)
		{
			push @offsets, $offset;
			$offset += length($&);
		}
		push @offsets, $offset;

		my $n = scalar(@offsets);
		die "Invalid header ($n): $line\n" unless scalar(@offsets) == 11;
		
		$self->{offsets} = \@offsets;
	}
	
	my @offsets = @{$self->{offsets}};
	my $id = $self->next_sequence_nr;
	my @data = ( $id );

	for (my $n = 0; $n < 10; ++$n)
	{
		my $v = substr($text, $offsets[$n], $offsets[$n + 1] - $offsets[$n]);
		
		$v =~ s/\s+$//;
		
		push @data, $v;
		
		   if ($n == 0) { $self->index_text('llhid', $v); }
		elsif ($n == 1) { $self->index_text('hsym', $v); }
		elsif ($n == 2) { $self->index_text('loc', $v); }
		elsif ($n == 3)
		{ 
			$v = substr($v, 4) if (substr($v, 0, 4) eq 'MGI:');
			$self->index_text('acc', $v);
		}
		elsif ($n == 4) { $self->index_text('llmid', $v); }
		elsif ($n == 5) { $self->index_text('msym', $v); }
		elsif ($n == 6) { $self->index_text('chr', $v); }
		elsif ($n == 7) { $self->index_text('cm', $v); }
		elsif ($n == 8) { $self->index_text('cb', $v); }
		elsif ($n == 9) { $self->index_text('data', $v); }
	}

	$self->set_attribute('id', $id);
	$self->index_unique_string('id', $id);
	$self->set_attribute('title', "Human: $data[1]; Mouse: $data[5]");
	$self->set_document(join("\t", @data));
}
