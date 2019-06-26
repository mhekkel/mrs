package M6::Script::pdbfinder;

our @ISA = "M6::Script";

my %NUMBER = (
    natom => 1,
    t_nres_nucl => 1,
    t_water_mols => 1,
    het_groups => 1,
    hssp_n_align => 1,
    t_nres_prot => 1,
    n_models => 1,
    t_non_std => 1,
    t_alternates => 1
);

my %FLOAT = (
    t_frac_helix => 1,
    t_frac_beta => 1,
    resolution => 1,
    r_factor => 1,
    free_r => 1
);

sub new
{
    my $invocant = shift;
    my $self = new M6::Script(
        header => qr'^//.*',
        lastdocline => '//',
        @_
    );
    return bless $self, "M6::Script::pdbfinder";
}

sub parse
{
    my ($self, $text) = @_;

    while ($text =~ m/^\s*([^: ]+)\s+:\s+(.+)\n/mg)
    {
        my $key = lc $1;
        my $value = $2;

        $key =~ s/-/_/g;

        if ($key eq 'id')
        {
            $value =~ s/\s+$//;
            $self->index_unique_string('id', $value);
            $self->set_attribute('id', $value);
        }
        elsif ($key eq 'header')
        {
            $self->index_text('header', $value);
            $self->set_attribute('title', lc $value);
        }
        elsif ($key eq 'author')
        {
            $value =~ s/(\w)\.(?=\w)/$1. /og;
#            $value =~ s/\./. /g;
            $self->index_text('author', $value);
        }
        elsif ($key eq 'chain')
        {
            last;
        }
        elsif ($key eq 'date')
        {
#            $self->index_date('date', $value);
            $self->index_string('date', $value);
        }
        elsif (defined $NUMBER{$key} and defined $value)
        {
            $self->index_number($key, $value * 1.0 * $NUMBER{$key});
        }
        elsif (defined $FLOAT{$key} and defined $value)
        {
            $self->index_float($key, $value * 1.0 * $FLOAT{$key});
        }
        else
        {
            if ($key =~ m/^[_a-z0-9]+$/io) {
                $self->index_text($key, $value);
            }
            else {
                print "WARNING: invalid key '$key'\n";
            }
        }
#        else
#        {
#            $self->index_text('text', $value);
#        }
    }
}

sub to_fasta
{
    my ($self, $text, $db, $id, $title) = @_;

    open(my $h, "<", \$text);
    my ($chainid, %seq);

    while (my $line = <$h>)
    {
        chomp($line);

        last if (substr($line, 0, 2) eq '//');

        if ($line =~ /^Chain\s*:\s*(\S)/)
        {
            $chainid = $1;
        }
        elsif ($line =~ /^\s*Sequence\s*:\s*([\-\w]+)/)
        {
            my $seq = uc $1;

            my @seqs = $seq =~ m/[ARNDCQEGHILKMFPSTWYVBZX]+/g;

            $seq{$chainid} = [@seqs];
        }
    }

    my $result = '';
    if (scalar keys %seq)
    {
        foreach my $chain (keys %seq)
        {
            my @seqs = @{$seq{$chain}};
            foreach my $seq (@seqs)
            {
                $result .= ">gnl|$db|$id|$chain $title\n$seq\n";
            }
        }
    }

    return $result;
}

1;
