package M6::Script::gene;

#use XML::LibXSLT;
#use XML::LibXML;

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

	my ($doc, $title, $last_id, $lookahead, $xml_header);
	my ($date_created, $date_updated, $date_discontinued);
	my ($date_kind, %date);
	
	$date{create} = ();
	$date{update} = ();
	$date{discontinue} = ();
	
	open(my $h, "<", \$text) or die "text? $!";
	
	while (my $line = <$h>)
	{
		$line =~ s|^\s+||;
		
		if ($line eq '</Entrezgene>')
		{
			foreach my $k ('create', 'update', 'discontinue')
			{
				next unless defined $date{$k}{year};
				my $date = sprintf("%4.4d-%2.2d-%2.2d", $date{$k}{year}, $date{$k}{month}, $date{$k}{day});
				$self->index_date("${k}d", $date);
			}
			
			$self->set_attribute('title', $title);
			last;
		}
		elsif ($line =~ m|^<Gene-track_geneid>(\d+)</Gene-track_geneid>|)
		{
			$self->index_unique_string('id', $1);
		}
		elsif ($line =~ m/<Gene-track_(create|update|discontinue)-date>/)
		{
			$date_kind = $1;
		}
		elsif ($line =~ m'^<Date-std_(year|month|day)>(\d+)</Date-std_\1>')
		{
			$date{$date_kind}{$1} = $2;
		}
		elsif ($line =~ m|^<(.+?)>(.+)</\1>|)
		{
			my ($key, $text) = ($1, $2);
			
			$self->index_text('text', $2);
			
			if ($key eq 'Prot-ref_name_E')
			{
				if (defined $title) {
					$title = "$title; $2";
				}
				else {
					$title = $2;
				}
			}
		}
	}
}

1;

