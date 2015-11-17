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

1;
