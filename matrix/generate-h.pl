#!/usr/bin/perl

use strict;
use warnings;
use Data::Dumper;

opendir(my $dh, ".");
foreach my $file (grep { -f and m/\.matrix/ } readdir($dh)) {
	&process($file);
}
closedir($dh);

exit;

sub process
{
	my ($file) = @_;
	
	my %m;
	my $nr = 0;
	
	open(my $f, "<$file");
	while (my $line = <$f>) {
		next if $line =~ m/^( |#|\*)/;
		chomp($line);
		my @values = split(m/\s+/, $line);
		my $r = shift @values;
		$r = lc $r if $r eq 'X';
		
		my %d = ( nr => $nr, m => \@values);
		
		$m{$r} = \%d;
		++$nr;
	}
	close($f);
	
	my @k = sort keys %m;
	my @ix = map { $m{$_}->{nr} } @k;
	
	my $name = sprintf("kM6%s%s%s", uc $1, $2, $3)
		if $file =~ m/(\w)(\w+)-(\d+)\.matrix/;
	print "const uint8 ${name}\[\] = {\n";
	
	for (my $i = 0; $i < scalar(@k); ++$i) {
		print "\t";
		my @m = @{$m{$k[$i]}->{'m'}};
		for (my $j = 0; $j <= $i; ++$j) {
			printf("%3.1d, ", $m[$ix[$j]]);
		}
	
		for (my $j = $i + 1; $j < scalar(@k); ++$j) {
			print  "     ";
		}
		
		print "// $k[$i]\n";
	}
	print "};\n\n";
}
