package M6::Script::medline;

use strict;
use warnings;

our @ISA = "M6::Script";

sub new
{
	my $invocant = shift;
	my $self = new M6::Script(
		firstdocline => qr|^\s*<MedlineCitation.+>|,
		lastdocline => qr|^\s*</MedlineCitation>|,

		mapped => {
			'AbstractText'		=> 'abstract',
			'ArticleTitle'		=> 'title',
			'Keyword'			=> 'keyword',
			'DescriptorName'	=> 'mesh',
			'LastName'			=> 'author',
			'ForeName'			=> 'author',
			'Initials'			=> 'author',
		},
		
		indices => {
			'id' => 'Identifier',
		#	'abstract' => 'AbstractText',
		#	'title' => 'ArticleTitle',
		#	'keyword' => 'Keyword',
			'abstract' => 'AbstractText',
			'title' => 'ArticleTitle',
			'keyword' => 'Keyword',
			'mesh' => 'Descriptor field, MESH term',
			'author' => 'Author name',
			'text' => 'Other text'
		},

		@_
	);
	return bless $self, "M6::Script::medline";
}

sub parse
{
	my ($self, $text) = @_;
	
	my $id;
	
	open(my $h, "<", \$text);

	while (my $line = <$h>)
	{
		$line =~ s|^\s+||;
		
		if ($line =~ m|^<MedlineCitation|)
		{
			if ($line =~ m|Owner="(.+?)"|)
			{
				$self->index_text('owner', $1);
			}

			if ($line =~ m|Status="(.+?)"|)
			{
				$self->index_text('status', $1);
			}
		}
		elsif ($line =~ m|^<PMID>(\d+)</PMID>|)
		{
			if (not defined $id)
			{
				$id = $1;
				$self->index_unique_string('id', $id);
				$self->set_attribute('id', $id);
			}
		}
		elsif ($line =~ m|<(.+?)>(.+)</\1>|)
		{
			if ($1 eq 'ArticleTitle')
			{
				my $title = $2;
				$title = substr($title, 0, 255) if length($title) > 255;
				$self->set_attribute('title', $title);
			}
			
			my $ix = $self->{mapped}{$1};
			$ix = 'text' unless defined $ix;
			
			$self->index_text($ix, $2);
		}
	}
}

1;
