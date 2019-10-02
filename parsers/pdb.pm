package M6::Script::pdb;

use strict;
use warnings;

our @ISA = "M6::Script";

our %aa_map = (
    'ALA'    =>    'A',
    'ARG'    =>    'R',
    'ASN'    =>    'N',
    'ASP'    =>    'D',
    'CYS'    =>    'C',
    'GLN'    =>    'Q',
    'GLU'    =>    'E',
    'GLY'    =>    'G',
    'HIS'    =>    'H',
    'ILE'    =>    'I',
    'LEU'    =>    'L',
    'LYS'    =>    'K',
    'MET'    =>    'M',
    'PHE'    =>    'F',
    'PRO'    =>    'P',
    'SER'    =>    'S',
    'THR'    =>    'T',
    'TRP'    =>    'W',
    'TYR'    =>    'Y',
    'VAL'    =>    'V',
    'GLX'    =>    'Z',
    'ASX'    =>    'B',
);

sub new
{
    my $invocant = shift;
    my $self = new M6::Script(@_);
    return bless $self, "M6::Script::pdb";
}

sub parse
{
    my ($self, $text, $filename) = @_;

    # *_final.pdb files (from pdb_redo) don't have the id in the header
    if( (lc $filename) =~ m/(.+\/)?([a-z0-9]{4})_final\.pdb/ )
    {
        my $id = $2;

        $self->set_attribute('id', $id);
        $self->index_unique_string('id', $id);
        $self->add_link('dssp', $id);
        $self->add_link('hssp', $id);
        $self->add_link('pdbfinder2', $id);
    }

    my ($header, $title, $compound, $model_count, %ligands);

    open(my $h, "<", \$text);

    while (my $line = <$h>)
    {
        my ($fld, $text) = ($1, $2) if $line =~ m/^(\S+)\s+(?:\d+\s+)?(.+)\n$/;

        next unless defined $fld ;

        if ($fld eq 'HEADER')
        {
            $header = $text;

            # xxxx is used in the pdb_redo files
            my $id = substr($line, 62, 4);
            if ( (lc $id) ne 'xxxx' )
            {
                $self->set_attribute('id', $id);
                $self->index_unique_string('id', $id);
                $self->add_link('dssp', $id);
                $self->add_link('hssp', $id);
                $self->add_link('pdbfinder2', $id);
            }
            $self->index_text('text', $text);
        }
        elsif ($fld eq 'MODEL')
        {
            ++$model_count;
        }
        elsif ($fld eq 'TITLE')
        {
            $title .= lc $text;
            $self->index_text('title', $text);
        }
        elsif ($fld eq 'COMPND')
        {
            if ($text =~ m/MOLECULE: (.+)/) {
                $compound .= lc $1 . ' ';
            }
            elsif ($text =~ m/EC: (.+?);/) {
                my $ec = $1;
                foreach my $ecc (split(m/, /, $ec)) {
                    $self->add_link('enzyme', $ecc);
                }
                $compound .= 'EC: ' . lc $ec . ' ';
            }
            $self->index_text('compnd', $text);
        }
        elsif ($fld eq 'KEYWDS')
        {
            foreach my $wrd (split(m/ /, $text)) {
                $self->index_string('keyword', $wrd);
            }
        }
        elsif ($fld eq 'AUTHOR')
        {
            # split out the author name, otherwise users won't be able to find them

            $text =~ s/(\w)\.(?=\w)/$1. /og;
            $self->index_text('ref', $text);
        }
        elsif ($fld eq 'JRNL')
        {
            $self->index_text('ref', $text);
        }
        elsif ($fld eq 'REMARK')
        {
            if ($text =~ /\s*(\d+\s+)?AUTH\s+(.+)/o)
            {
                $text = $2;
                $text =~ s/(\w)\.(?=\w)/$1. /og;
            }

            $self->index_text('remark', $text);
        }
        elsif ($fld eq 'DBREF')
        {
# 0         1         2         3         4         5         6
# DBREF  2IGB A    1   179  UNP    P41007   PYRR_BACCL       1    179
            my $db = substr($line, 26, 7);    $db =~ s/\s+$//;
            my $ac = substr($line, 33, 9);    $ac =~ s/\s+$//;
            my $id = substr($line, 42, 12);    $id =~ s/\s+$//;

            my %dbmap = (
                embl    => 'embl',
                gb        => 'genbank',
                ndb        => 'ndb',
                pdb        => 'pdb',
                pir        => 'pir',
                prf        => 'profile',
                sws        => 'uniprot',
                trembl    => 'uniprot',
                unp        => 'uniprot'
            );

            $db = $dbmap{lc($db)} if defined $dbmap{lc($db)};

            if (length($db) > 0)
            {
                $self->add_link($db, $id) if length($id);
                $self->add_link($db, $ac) if length($ac);
            }
        }
        elsif ($fld eq 'HETATM') {
            my $ligand = substr($line, 17, 3);
            $ligands{$ligand} = 1;
        }
    }

    $self->index_number('models', $model_count) if defined $model_count;

    if (defined $header)
    {
        $header = "$title ($header)" if (defined $title and length($title) > 0);
        $header .= "; $compound" if (defined $compound and length($compound) > 0);
        $header = substr($header, 0, 255) if (length($header) > 255);
        $header =~ s/ {2,}/ /g;
        $self->set_attribute('title', $header);
    }

    foreach my $ligand (keys %ligands)
    {
        $ligand =~ s/^\s*(\S+)\s*$/$1/;
        $self->index_string("ligand", $ligand);
    }
}

sub to_fasta
{
    my ($self, $text, $db, $id, $title) = @_;

    my %seq;

    while ($text =~ m/^SEQRES.+/mg)
    {
        my $chainId = substr($&, 11, 1);
        my $r = substr($&, 19);

        $seq{$chainId} = '' unless defined $seq{$chainId};

        foreach my $res (split(m/ /, $r))
        {
            next unless length($res) == 3;
            my $aa = $aa_map{$res};
            $aa = 'X' unless defined $aa;
            $seq{$chainId} .= $aa;
        }
    }

    my $result = '';
    $title = '' unless $title;

    foreach my $seq (keys %seq)
    {
        my $s = $seq{$seq};
        next unless length($s) > 0;

        $s =~ s/(.{72})/$&\n/g;
        $result .= ">gnl|$db|$id|$seq $title\n$s\n";
    }

    return $result;
}

1;
