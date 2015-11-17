#!perl

BEGIN
{
    if ($^O eq 'MSWin32')
    {
        push @INC,"C:/perl/site/lib";
        push @INC,"C:/perl/lib";
    }
}

package M6;

our $VERSION = '6.0';

#use strict;
#use warnings;
#use Data::Dumper;

my %scripts;
my %INDICES;

sub valid_package_name
{
    my ($string) = @_;

    $string =~ s/([^A-Za-z0-9\/])/sprintf("_%2x",unpack("C",$1))/eg;
    # second pass only for words starting with a digit
    $string =~ s|/(\d)|sprintf("/_%2x",unpack("C",$1))|eg;

    # Dress it up as a real package name
    $string =~ s|/|::|g;

    return "Embed" . $string;
}

sub load_script
{
    my ($mrs_script_dir, $name) = @_;

    my $package = valid_package_name($name);

    my $script_name = "M6::Script";
    my $indices;

    if ($package ne 'default')
    {
        my $plugin = "${mrs_script_dir}/${name}.pm";
        my $mtime = -M $plugin;

        if (not defined $scripts{$name} or $scripts{$name} != $mtime)
        {
            my $fh;
            open $fh, $plugin or die "open '$plugin' $!";
            local($/) = undef;
            my $sub = <$fh>;
            close $fh;

            # wrap the code into a subroutine inside our unique package
            my $eval = qq{package $package; sub handler { $sub; }};
            {
                # hide our variables within this block
                my ($mrs_script_dir, $package, $sub, $name, $mtime, $plugin);
                eval $eval;
            }
            die $@ if $@;

            eval {
                $package->handler;
            };
            die $@ if $@;

            $scripts{$name} = $mtime;
        }

        $script_name .= "::$name";
    }

    return $script_name->new('script_dir' => $mrs_script_dir);
};

package M6::Script;

#use strict;

require Exporter;
require DynaLoader;

our @ISA = qw(Exporter DynaLoader M6);
our @EXPORT = qw( );

sub new
{
    my $pkg = shift;
    my $self = M6::new_M6_Script(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY
{
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    M6::delete_M6_Script($self);
}

sub TIEHASH
{
    my ($classname, $obj) = @_;
    return bless $obj, $classname;
}

sub version
{
    my ($self, $source) = @_;
    die("version not implemented");
}

sub to_fasta
{
    my ($self, $doc) = @_;
    die("to_fasta not implemented");
}

sub index_name
{
    my ($self, $index) = @_;

    my $result = $self->{indices}->{$index};

    $result = $result->{name}
        if (defined $result and ref($result) eq 'HASH');

    $result = $index unless defined $result;

    return $result;
}

1;
