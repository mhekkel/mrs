package MRS::Script::uniprot;

use utf8;

BEGIN			# start by defining our DEParser object. This sucks, should be able to do this more elegant
{
	our $RECNAME = 1;
	our $ALTNAME = 2;
	our $SUBNAME = 3;
	our $FULL = 4;
	our $SHORT = 5;
	our $EC = 6;
	our $ALLERGEN = 7;
	our $BIOTECH = 8;
	our $CD_ANTIGEN = 9;
	our $INN = 10;
	
	our $TEXT = 11;
	
	our $INCLUDES = 12;
	our $CONTAINS = 13;
	
	our $FLAGS = 14;
	
	our $END = 0;
	
	our %TOKENS = (
		$RECNAME	=> qr/^\s*RecName: /,
		$ALTNAME	=> qr/^\s*AltName: /,
		$SUBNAME	=> qr/^\s*SubName: /,
		$FULL		=> qr/^\s*Full=/,
		$SHORT		=> qr/^\s*Short=/,
		$EC			=> qr/^\s*EC=/,
		$ALLERGEN	=> qr/^\s*Allergen=/,
		$BIOTECH	=> qr/^\s*Biotech=/,
		$CD_ANTIGEN	=> qr/^\s*CD_antigen=/,
		$INN		=> qr/^\s*INN=/,
		
		$INCLUDES	=> qr/^Includes:/,
		$CONTAINS	=> qr/^Contains:/,
		
		$FLAGS		=> qr/^Flags: /
	);
	
	sub DEParser::new
	{
		my ($invocant, $de) = @_;
		my $self = {
			'text'	=> $de,
			@_
		};
		return bless $self, "DEParser";
	}
	
	sub DEParser::parse()
	{
		my $self = shift;
		
		my $h;
		open($h, "<", \$self->{text});
		$self->{handle} = $h;
		
		$self->{lookahead} = $self->nextToken();
		
		my %result;
		
		$result{'name'} = $self->parseNextName()
			if ($self->{lookahead} == $RECNAME);
		
		while ($self->{lookahead} == $INCLUDES or $self->{lookahead} == $CONTAINS or $self->{lookahead} == $SUBNAME)
		{
			if ($self->{lookahead} == $INCLUDES) {
				$self->match($INCLUDES);
				$result{'includes'} = [] unless defined $result{'includes'};
				push @{$result{'includes'}}, $self->parseNextName();
			}
			elsif ($self->{lookahead} == $CONTAINS) {
				$self->match($CONTAINS);
				$result{'contains'} = [] unless defined $result{'contains'};
				push @{$result{'contains'}}, $self->parseNextName();
			}
			else
			{
				$self->match($SUBNAME);
				$result{'sub'} = [] unless defined $result{'sub'};
				push @{$result{'sub'}}, $self->parseName();
			}
		}
		
		while ($self->{lookahead} == $FLAGS)
		{
			$self->match($FLAGS);
			$result{'flags'} = [] unless defined $result{'flags'};
			push @{$result{'flags'}}, $self->{tokenvalue};
			$self->match($TEXT);
		}
		
		warn "Text was not parsed entirely\n" unless $self->{lookahead} == $END;
		
		close($h);
	
		return \%result;
	}
	
	sub DEParser::nextToken
	{
		my $self = shift;
		
		my $handle = $self->{handle};
		
		if (not defined $self->{line} or length($self->{line}) == 0)
		{
			$self->{line} = <$handle>;
			chomp($self->{line});
		}
		
		return $END unless defined $self->{line} and length($self->{line}) > 0;
	
	#print "next token from line: $self->{line}\n";
		
		foreach my $token (keys %TOKENS)
		{
			if ($self->{line} =~ $TOKENS{$token}) {
				$self->{tokenvalue} = $&;
				$self->{line} = substr($self->{line}, length($&));
	#print "returning $token, '$&'\n";
				return $token;
			}
		}
		
		# remaining text on line is considered to be TEXT
		# chop off the trailing semicolon, if it exits
		
		$self->{line} =~ s/;$//;
		
		$self->{tokenvalue} = $self->{line};
		$self->{line} = undef;
	
	#print "returning $TEXT, '$self->{line}'\n";
		return $TEXT;
	}
	
	sub DEParser::parseNextName
	{
		my $self = shift;
		
		my %name;
		
		if ($self->{lookahead} == $RECNAME)
		{
			$self->match($RECNAME);
			$name{'name'} = $self->parseName();
		}
		
		while ($self->{lookahead} == $ALTNAME)
		{
			$self->match($ALTNAME);
			
			$name{'alt'} = [] unless defined $name{'alt'};
			
			if ($self->{lookahead} == $ALLERGEN)
			{
				$self->match($ALLERGEN);
				push @{$name{'alt'}}, "allergen: " . $self->{tokenvalue};
				$self->match($TEXT);
			}
			elsif ($self->{lookahead} == $BIOTECH)
			{
				$self->match($BIOTECH);
				push @{$name{'alt'}}, "biotech: " . $self->{tokenvalue};
				$self->match($TEXT);
			}
			elsif ($self->{lookahead} == $CD_ANTIGEN)
			{
				$self->match($CD_ANTIGEN);
				push @{$name{'alt'}}, "cd antigen: " . $self->{tokenvalue};
				$self->match($TEXT);
			}
			elsif ($self->{lookahead} == $INN)
			{
				$self->match($INN);
				push @{$name{'alt'}}, "INN: " . $self->{tokenvalue};
				$self->match($TEXT);
			}
			else
			{
				push @{$name{'alt'}}, $self->parseName();
			}
		}
	
		return \%name;
	}
	
	sub DEParser::parseName
	{
		my $self = shift;
		
		my %name = ();
		
		for (;;)
		{
			if ($self->{lookahead} == $FULL) {
				$self->match($FULL);
				$name{'full'} = $self->{tokenvalue};
				$self->match($TEXT);
			}
			elsif ($self->{lookahead} == $SHORT) {
				$self->match($SHORT);
				$name{'short'} = [] unless defined $name{'short'};
				push @{$name{'short'}}, $self->{tokenvalue};
				$self->match($TEXT);
			}
			elsif ($self->{lookahead} == $EC) {
				$self->match($EC);
				$name{'ec'} = [] unless defined $name{'ec'};
				push @{$name{'ec'}}, $self->{tokenvalue};
				$self->match($TEXT);
			}
			else {
				last;
			}
		}
		
		my $name = '';
		if (defined $name{'full'})
		{
			$name = $name{'full'};
			$name .= " (" . join("; ", @{$name{'short'}}) . ")"
				if (defined $name{'short'});
		}
		else
		{
			$name = join("; ", @{$name{'short'}});
		}
		
		if ($self->{link_ec}) {
			$name .= ", " . join("; ", map { "<mrs:link db='enzyme' id='$_'>EC:$_</mrs:link>" } @{$name{'ec'}})
				if (defined $name{'ec'});
		}
		else {
			$name .= ", " . join("; ", map { "EC:$_" } @{$name{'ec'}})
				if (defined $name{'ec'});
		}
		
		return $name;
	}
	
	sub DEParser::match
	{
		my ($self, $match) = @_;
	
		die "Parse error, expected $match but found $self->{lookahead}\n"
			unless $match == $self->{lookahead};
		$self->{lookahead} = $self->nextToken();
	}
	
	sub DEParser::formattedName
	{
		my ($self, $q, $name, $title) = @_;
	
		my @result;
		
		my @synonyms = map { $q->td($_) } @{$name->{alt}};
		push @result, $q->Tr($q->td({-rowspan=>(scalar @synonyms) + 1}, $title), $q->td($name->{name}));	
		push @result, $q->Tr(\@synonyms) if scalar @synonyms;
		
		return @result;
	}
}

our @ISA = "MRS::Script";

use strict;
use warnings;
use POSIX qw/strftime/;
use Data::Dumper;

our $commentLine1 = "CC   -----------------------------------------------------------------------";
our $commentLine2 = "CC   Copyrighted by the UniProt Consortium, see http://www.uniprot.org/terms";
our $commentLine3 = "CC   Distributed under the Creative Commons Attribution-NoDerivs License";

our %INDICES = (
	id			=> 'Identification',
	ac			=> 'Accession number',
	cc			=> 'Comments and Notes',
	dt			=> 'Date',
	de			=> 'Description',
	gn			=> 'Gene name',
	os			=> 'Organism species',
	og			=> 'Organelle',
	oc			=> 'Organism classification',
	ox			=> 'Taxonomy cross-reference',
	'ref'		=> 'Any reference field',
	dr			=> 'Database cross-reference',
	kw			=> 'Keywords',
	ft			=> 'Feature table data',
	sv			=> 'Sequence version',
	fh			=> 'Feature table header',
	crc64		=> 'The CRC64 checksum for the sequence',
	'length'	=> 'The length of the sequence',
	mw			=> 'Molecular weight',
);

sub new
{
	my $invocant = shift;

#	my %merge_databanks = (
#		uniprot => [ 'sprot', 'trembl' ],
#		sp300	=> [ 'sp100', 'sp200' ],		# for debugging purposes
#	);

#	my $self = new MRS::Script(
	my $self = {
		attr					=> [ 'title', 'acc' ],
		indices					=> \%INDICES,
		@_
	};
	
	return bless $self, "MRS::Script::uniprot";
}

#sub version
#{
#	my ($self) = @_;
#
#	my $raw_dir = $self->{raw_dir} or die "raw_dir is not defined\n";
#	my $db = $self->{db} or die "db is not defined\n";
#	
#	$raw_dir =~ s|$db/?$|uniprot|;
#	my $version;
#
#	open RELDATE, "<$raw_dir/reldate.txt";
#	while (my $line = <RELDATE>)
#	{
#		if ($db eq 'sprot' and $line =~ /Swiss-Prot/) {
#			$version = $line;
#			last;
#		}
#		elsif ($db eq 'trembl' and $line =~ /TrEMBL/) {
#			$version = $line;
#			last;
#		}
#	}
#	close RELDATE;
#
#	$version = $self->SUPER::version() unless defined $version;
#	chomp($version);
#	
#	return $version;
#}

sub parse
{
	my ($self, $text) = @_;
	
	my ($id, $acc, $doc, $title, $species, $m);
	
	my %months = (
		'JAN'	=> 1,
		'FEB'	=> 2,
		'MAR'	=> 3,
		'APR'	=> 4,
		'MAY'	=> 5,
		'JUN'	=> 6,
		'JUL'	=> 7,
		'AUG'	=> 8,
		'SEP'	=> 9,
		'OCT'	=> 10,
		'NOV'	=> 11,
		'DEC'	=> 12
	);
	
	while ($text =~ m/^(?:([A-Z]{2})   ).+\n(?:\1.+\n)*/gm)
	{
		my $key = $1;
		my $value = $&;
		
		$value =~ s/^$key   //m ;

		if ($key eq 'ID')
		{
			$value =~ m/^(\w+)/ or die "No ID in UniProt record?\n$key   $value\n";
			$self->index_unique_string('id', $1);
			$self->store_attribute('id', $1);
			next;
		}

		if ($key eq 'AC')
		{
			foreach my $ac (split(m/;\s*/, $value))
			{
				$self->index_unique_string('ac', $ac);
				$self->store_attribute('ac', $ac);
			}
			next;
		}

		
		
		
#		
#		
#		
#		$doc .= $line;
#
#		chomp($line);
#
#		# just maybe there are records without a sequence???
#		if ($line eq '//')
#		{
#			die "Error in swissprot parser, missing sequence section\n";
#		}
#		elsif ($line =~ /^([A-Z]{2}) {3}(.+)/o)
#		{
#			my $fld = $1;
#			my $value = $2;
#
#			if ($line =~ /^ID +(\S+)/o)
#			{
#				$id = $1;
#				$self->IndexValue('id', $id);
#			}
#			elsif ($fld eq 'SQ')
#			{
#				if ($line =~ /SEQUENCE\s+(\d+) AA;\s+(\d+) MW;\s+([0-9A-F]{16}) CRC64;/o)
#				{
#					$self->IndexNumber('length', $1);
#					$self->IndexNumber('mw', $2);
#					$self->IndexWord('crc64', $3);
#				}
#				
#				my $sequence = "";
#				
#				while ($line = $self->GetLine)
#				{
#					$doc .= $line;
#					chomp($line);
#
#					last if $line eq '//';
#					
#					$line =~ s/\s//g;
#					$sequence .= $line;
#				}
#
#				# new format for DE records:
#				$title = $1 if ($title =~ m/Full=(.+);?/);
#				
#				$self->StoreMetaData('title', $title)		if defined $title;
#				$self->StoreMetaData('acc', $acc)			if defined $acc;
##				$self->StoreMetaData('species', $species)	if defined $species;
#				$self->Store($doc);
#				$self->AddSequence($sequence);
#				$self->FlushDocument;
#	
#				$id = undef;
#				$acc = undef;
#				$doc = undef;
#				$title = undef;
#				$species = undef;
#			}
#			elsif (substr($fld, 0, 1) eq 'R')
#			{
#				$self->IndexTextAndNumbers('ref', $value);
#			}
#			elsif ($fld eq 'CC')
#			{
#				if ($line ne $commentLine1 and
#					$line ne $commentLine2 and
#					$line ne $commentLine3)
#				{
#					$self->IndexTextAndNumbers('cc', $value);
#				}
#			}
#			elsif ($fld eq 'DE')
#			{
#				$title .= "\n" if defined $title;
#				$title .= $value;
#				
#				$self->IndexTextAndNumbers('de', $value);
#
#				if ($value =~ /(EC\s*)(\d+\.\d+\.\d+\.\d+)/)
#				{
#					$self->IndexLink('enzyme', $2);
#				}
#			}
#			elsif ($fld eq 'OS')
#			{
#				$species .= ' ' if defined $species;
#				$species .= $value;
#				
#				$self->IndexTextAndNumbers('os', $value);
#			}
#			elsif ($fld eq 'DT')
#			{
#				if ($value =~ m/(\d{2})-([A-Z]{3})-(\d{4})/) {
#					my $date = sprintf('%4.4d-%2.2d-%2.2d', $3, $months{$2}, $1);
#					
#					eval { $self->IndexDate('dt', $date); };
#					
#					warn $@ if $@;
#				}
#			}
#			elsif ($fld eq 'DR')
#			{
#				if ($value =~ m/\s*(.+?); (.+?); (.+?)\./)
#				{
#					my $db = $1;
#					$id = $2;
#
#					$id = $3 if ($links{$1}->{value} == 2);
#
#					$self->IndexLink($db, $id);
#				}
#				
#				$self->IndexTextAndNumbers(lc($fld), $value);
#			}
#			elsif ($fld eq 'AC' or $fld eq 'OH' or $fld eq 'OX' or $fld eq 'OC' or $fld eq 'DR')
#			{
#				if ($fld eq 'AC' and not defined $acc) {
#					($acc) = split(m/;/, $value);
#				}
#				
##				$self->IndexTextAndNumbers(lc($fld), $value, 0);
#				$self->IndexTextAndNumbers(lc($fld), $value);
#			}
#			elsif ($fld ne 'XX')
#			{
#				$self->IndexTextAndNumbers(lc($fld), $value);
#			}
#		}
	}
}

1;
