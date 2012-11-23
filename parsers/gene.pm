package M6::Script::gene;

use strict;
use warnings;

our @ISA = "M6::Script";

sub new
{
	my $invocant = shift;
	my $self = new M6::Script(
		firstdocline => '  <Entrezgene>',
		lastdocline => '  </Entrezgene>',
		@_
	);
	return bless $self, "M6::Script::gene";
}

sub parse
{
	my ($self, $text) = @_;

	if ($text =~ m|<Gene-track_geneid>(\d+)</Gene-track_geneid>|)
	{
		$self->set_attribute('id', $1);
		$self->index_unique_string('id', $1);
	}
	
	my $title = '';
	while ($text =~ m|<Prot-ref_name_E>(.+?)</Prot-ref_name_E>|g)
	{
		$title .= "; $1";
	}

	$self->set_attribute('title', $title) if length($title) > 0;	
	
	# index attribute values
	while ($text =~ m|<\S+\s([^>]+)>|g)
	{
		my $attr = $1;
		while ($attr =~ m/\S+?=('|")([^'"]+)\1/g)
		{
			$self->index_text('attr', $2);
		}
	}

	# index content
	while ($text =~ m|>([^<>]++)<|g)
	{
		$self->index_text('text', $1);
	}
	
}

1;

