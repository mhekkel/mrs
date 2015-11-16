package M6::Script::vcf;

our @ISA = "M6::Script";

use Data::Dumper;
use POSIX;

sub new
{
	my $invocant = shift;
	my $self = new M6::Script(
		header => qr/^#.+(\n#.+)*/,
		firstdocline => qr/.+/,
		@_
	);
	return bless $self, "M6::Script::vcf";
}

sub header
{
	my ($self, $filename, $header) = @_;
	
	die "Not a VCF file, fileformat is missing" unless $header =~ m/^##fileformat=VCFv(\d)\.(\d)/im;
	warn "Unknown version of VCF file" unless $1 == 4 and $2 == 1;
	
	die "Missing #CHROM line" unless $header =~ m/^#(CHROM\t.+)/im;

	$self->{filename} = $filename;
	
	push @{$self->{columns}}, split(m/\t/, $1);
	
	my $n = 0;
	my $line_count = -1;
	
	foreach my $column (@{$self->{columns}})
	{
		$self->{column_index}{$column} = $n;
		++$n;
		++$line_count if ($line_count >= 0 or $column eq 'FORMAT');
		push @{$self->{lines}}, $column if $line_count > 0;
	}

	while ($header =~ m/^##INFO=<ID=(.+?),Number=.,Type=(.+?),Description="(.+?)">/gm)
	{
		my $column = $1;
		$self->{column_type}{$column} = $2;
		$self->{column_desc}{$column} = $3;
	}

	while ($header =~ m/^##FORMAT=<ID=(.+?),Number=(.+?),Type=(.+?),Description="(.+?)">/gm)
	{
		my $column = $1;
		$self->{format_nmbr}{$column} = $2;
		$self->{format_type}{$column} = $3;
		$self->{format_desc}{$column} = $4;
	}
}

sub parse
{
	my ($self, $text, $filename, $header) = @_;
	
	chomp($text);

	$self->header($filename, $header) if not defined $self->{column_index} or $self->{filename} ne $filename;
	
	my @columns = split(m/\t/, $text);

	my $id = $self->next_sequence_nr();

	$self->set_attribute('id', $id);
	$self->set_attribute('title', sprintf("%s/%d %s => %s",
		$columns[$self->{column_index}{CHROM}],
		$columns[$self->{column_index}{POS}],
		$columns[$self->{column_index}{REF}],
		$columns[$self->{column_index}{ALT}]));

	my $pos = int($columns[$self->{column_index}{POS}]);
	my $chr = $columns[$self->{column_index}{CHROM}];
	
	$self->index_unique_string('id', $id);
	$self->index_string('chrom', $chr);

	$chr = $1 if $chr =~ m/(ch\d+)/;

	$self->index_number('pos', $pos);
#	$self->index_number('pos_bin', int($pos / 1000));
	$self->index_string('pos_chr', sprintf("%s_%d", $chr, $pos));
#	$self->index_string('pos_chr_bin', sprintf("%s_%d", $chr, int($pos / 1000)));
	$self->index_number('pos_at_' . $chr, $pos);
	
	$self->index_float('qual', $columns[$self->{column_index}{QUAL}]);
	
	my $fmtnr = 0;
	my %FORMAT = map { $_ => $fmtnr++ } split(m/:/, $columns[$self->{column_index}{FORMAT}]);
	
	my $line_count = scalar @{$self->{lines}};
	my @gq = (0) x $line_count;
	my @dp = (0) x $line_count;

	for (my $line_nr = 0; $line_nr < $line_count; ++$line_nr)
	{
		my $line = $self->{lines}[$line_nr];
		
		my $value = $columns[$self->{column_index}{$line}];

		next if $value eq '0/0';
		
		$self->index_string('line', $line);
		
		# split the format values
		
		my @values = split(m/:/, $value);

		my $gq = POSIX::floor($values[$FORMAT{GQ}]);
#		$self->index_number("ind_gq-$line", $gq);
		$self->index_float("ind_gq", $gq) if defined $gq;
		$gq[$line_nr] = $gq;

		my $dp = $values[$FORMAT{DP}];
#		$self->index_number("ind_dp-$line", $dp);
		$self->index_float("ind_dp", $dp) if defined $dp;
		$dp[$line_nr] = $dp;
	}
	
	$self->set_attribute('gq', join(';', @gq));
	$self->set_attribute('dp', join(';', @dp));

	foreach my $info (split(m/;/, $columns[$self->{column_index}{INFO}]))
	{
		my ($key, $value) = split(m/=/, $info);
		
		next unless defined $self->{column_type}{$key};

		if ($self->{column_type}{$key} eq 'Float')
		{
			$self->index_float($key, $value);
		}
		elsif ($self->{column_type}{$key} eq 'Integer')
		{
			$self->index_number($key, $value);
		}
		elsif ($key ne 'EFF')
		{
			$self->index_string($key, $value);
		}	
	}
}

1;
