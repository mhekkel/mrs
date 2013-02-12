package M6::Script::uniref;

use strict;
use warnings;

our @ISA = "M6::Script";

sub new
{
	my $invocant = shift;

	my $self = new M6::Script(
		firstdocline => qr/^<entry .+/,
		lastdocline => '</entry>',
		indices		=> {
			'id'		=> 'Identifier',
			'title'		=> 'ArticleTitle',
			'member'	=> 'Member ID',
			'updated'	=> 'Updated',
			'acc'		=> 'Member Accession',
		},
		@_
	);
	
	return bless $self, "M6::Script::uniref";
}

sub parse
{
	my ($self, $text) = @_;

	my $title;
	
	open(my $h, "<", \$text);
	while (my $line = <$h>)
	{
		chomp($line);

		if ($line =~ m|<entry id="(.+?)" updated="(.+?)">|) {
			$self->index_unique_string('id', $1);
			$self->set_attribute('id', $1);
#			$self->IndexDate('updated', $2);
			$self->index_string('updated', $2);
		}
		elsif ($line =~ m|<name>(.+)</name>|) {
			$self->index_text('name', $1);
			$title = $1 unless defined $title;
		}
		elsif ($line =~ m|<property type="(.+?)" value="(.+?)"/?>|) {
			my ($name, $value) = ($1, $2);
			if ($name eq 'UniProtKB accession') {
				$self->index_text('acc', $value);
			}
		}
		elsif ($line =~ m|<dbReference type="(.+?)" id="(.+?)"/?>|) {
			my ($name, $value) = ($1, $2);
			if ($name eq 'UniProtKB ID') {
				$self->index_string('member', $value);
			}
#			else {
#				warn "Unknown dbReference type: $name\n";
#			}
		}
		elsif ($line =~ m|<sequence|) {
			my $seq = '';
			for (;;)
			{
				$line = <$h>;
				last if $line =~ m|</sequence>|;
				$seq .= $line;
			}
#			$self->AddSequence($seq);
		}
	}

	$self->set_attribute('title', $title);
}

1;
