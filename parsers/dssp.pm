package M6::Script::dssp;

our @ISA = "M6::Script";

sub new
{
    my $invocant = shift;
    my $self = new M6::Script(@_);
    return bless $self, "M6::Script::dssp";
}

sub parse
{
    my ($self, $text) = @_;

    my $title;

    while ($text =~ m/^.+$/mg)
    {
        my $line = $&;

        last if (substr($line, 0, 1) eq ' ');

        if ($line =~ /^HEADER/o)
        {
            $title = substr($line, 10, 40);
            my $id = substr($line, 62, 4);

            $self->index_unique_string('id', $id);
            $self->set_attribute('id', $id);
        }
        elsif ($line =~ /^COMPND.*?(?:MOLECULE: )?(?: |\d )(.+)/mo)
        {
            my $cmp = $1;
            $cmp =~ s/\s+;?\s*$//;
            $title .= '; ' . lc($cmp);
        }
        elsif ($line =~ /^AUTHOR\s+(.+)/o)
        {
            my $author = $1;
            $author =~ s/(\w)\.(?=\w)/$1. /og;
            $self->index_text('ref', $author);
        }
        else
        {
            $self->index_text('text', $line);
        }
    }

    $self->set_attribute('title', $title);
}

my $RESIDUE_HEADER = '  #  RESIDUE AA STRUCTURE BP1 BP2  ACC     N-H-->O    O-->H-N    N-H-->O    O-->H-N    TCO  KAPPA ALPHA  PHI   PSI    X-CA   Y-CA   Z-CA            CHAIN AUTHCHAIN';

sub to_fasta
{
    my ($self, $text, $db, $id, $title) = @_;

    open(my $h, "<", \$text);
    my ($chainid, %seq, @seqs);

    my $reading_residues = 0;
    while (my $line = <$h>)
    {
        $line =~ s/\n$//g;

        if ($line eq $RESIDUE_HEADER)
        {
            $reading_residues = 1;
        }
        elsif ($reading_residues)
        {
            my $ch = substr $line, 154, 9;
            $ch =~ s/^\s+|\s+$//g;

            if ((length $ch) > 0)
            {
                $chainid = $ch;

                if (not defined $seq{$chainid})
                {
                    $seq{$chainid} = '';
                }
            }

            my $aa = substr $line, 13, 1;

            $seq{$chainid} .= $aa;
        }
    }

    my $result = '';
    if (scalar keys %seq)
    {
        foreach my $chain (keys %seq)
        {
            my @seqs = split /!/, $seq{$chain};
            foreach my $seq (@seqs)
            {
                if ((length $seq) > 0 and (length $chain) > 0)
                {
                    $result .= ">gnl|$db|$id|$chain $title\n$seq\n";
                }
            }
        }
    }

    return $result;
}

1;
