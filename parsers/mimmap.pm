package M6::Script::mimmap;

use strict;
use warnings;

our @ISA = "M6::Script";

sub new
{
	my $invocant = shift;
	my $self = new M6::Script(
		firstdocline => qr/^.+/,
		@_
	);
	
	return bless $self, "M6::Script::mimmap";
}

sub parse
{
	my ($self, $text) = @_;
	
	# $text contains only one line

	chomp($text);
	
	# convert genemap to mimmap
	
	my @data = split(m/\|/, $text);
	
	my %status = (
		'C' =>	'confirmed',
		'P'	=>	'provisional',
		'I'	=>	'inconsistent',
		'L'	=>	'tentative'
	);
	
	my $id = $self->next_sequence_nr;
	my $dis = join(' ', $data[13], $data[14], $data[15]);

	my @mim = split(m/\D+/, $dis);
	my $mim = join(", ", grep { $_ =~ m/\d{6}/ } @mim);
	
	my $doc=<<"EOF";
ID      : $id
Code    : $data[0]
MIM#    : $data[9]
Date    : $data[3].$data[1].$data[2]
Location: $data[4]
Symbol  : $data[5]
Status  : $data[6] ($status{$data[6]})
Title   : $data[7] $data[8]
Method  : $data[10]
Comment : $data[11] $data[12]
Disorder: $dis
MIM_Dis : $mim
Mouse   : $data[16]
Refs    : $data[17]
//		
EOF
		
	$self->index_unique_string('id', $id);
	$self->index_string('mim', $data[9]);

	my ($year, $mon, $day) = ($data[3], $data[1], $data[2]);
	$year += 1900;
	$year += 100 if $year < 1970;

#	$self->index_date('date', sprintf("%4.4d-%2.2d-%2.2d", $year, $mon, $day));
	$self->index_string('date', sprintf("%4.4d-%2.2d-%2.2d", $year, $mon, $day));
	
	$self->index_string('code', $data[0])				if defined $data[0];
	$self->index_string('location', $data[4])			if defined $data[4];
	$self->index_string('status', $status{$data[6]})	if defined $data[6];
	$self->index_text('title', join(' ', $data[7], $data[8]));
	$self->index_text('method', $data[10])			if defined $data[10];
	$self->index_text('symbol', $data[5])			if defined $data[5];
	$self->index_text('comment', join(' ', $data[11], $data[12]));
	$self->index_text('disorder', $dis);
	$self->index_text('mim_dis', $mim);
	$self->index_text('mouse', $data[16])			if defined $data[16];
	$self->index_text('refs', $data[17])				if defined $data[17];

	$self->set_attribute('title', "$data[7] $data[9]");
	$self->set_document($doc);
}

1;
