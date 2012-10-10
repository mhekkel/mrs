package M6::Script::dbest;

use strict;
use warnings;

our @ISA = "M6::Script";

sub new
{
	my $invocant = shift;
	my $self = new M6::Script(
		lastdocline => '||',
		@_
	);
	return bless $self, "M6::Script::dbest";
}

sub parse
{
	my ($self, $text) = @_;
	
	my ($est, $os, $lib);
	
	open(my $h, "<", \$text);

	while (my $line = <$h>)
	{
		chomp($line);

		if ($line eq '||')
		{
			$self->set_attribute('title', "$est; $os; $lib");
			last;
		}
		elsif ($line =~ m/^dbEST Id:\s+(\d+)/)
		{
			$self->set_attribute('id', $1);
			$self->index_unique_string('id', $1);
		}
		elsif ($line =~ m/^EST name:\s+(.+)/)
		{
			$est = $1;
			$self->index_text('source_est', $est);
		}
		elsif ($line =~ m/^Clone Id:\s+(.+)/)
		{
			$self->index_text('clone', $1);
		}
		elsif ($line =~ m/^GenBank gi:\s+(.+)/)
		{
			$self->index_string('genbank_gi', $1);
		}
		elsif ($line =~ m/^GenBank Acc:\s+(.+)/)
		{
			$self->index_string('genbank_acc', $1);
		}
		elsif ($line =~ m/^Source:\s+(.+)/)
		{
			$self->index_text('source', $1);
		}
		elsif ($line =~ m/^Organism:\s+(.+)/)
		{
			$os = $1;
			$self->index_text('organism', $os);
		}
		elsif ($line =~ m/^Lib Name:\s+(.+)/)
		{
			$lib = $1;
			$self->index_text('lib_name', $lib);
		}
		elsif ($line =~ m/^dbEST Lib id:\s+(.+)/)
		{
			$self->index_text('lib_id', $1);
		}
		elsif ($line =~ m/^DNA Type:\s+(.+)/)
		{
			$self->index_text('dna_type', $1);
		}
		elsif ($line =~ m/^Map:\s+(.+)/)
		{
			$self->index_text('chr', $1);
		}
		elsif ($line =~ m/^GDB Id:\s+(.+)/)
		{
			$self->index_text('gdb', $1);
		}
		elsif ($line =~ m/^SEQUENCE/)
		{
			do { $line = <$h>; } while (substr($line, 0, 16) eq ' ' x 16);
		}
		elsif (length($line) > 16)
		{
			$self->index_text('text', substr($line, 16, length($line) - 16));
		}
	}
}

1;
