#!perl


BEGIN
{
	if ($^O eq 'MSWin32')
	{
		push @INC,"C:/perl/site/lib";
		push @INC,"C:/perl/lib";
	}
}

use Data::Dumper;
use LWP::Simple qw(get);

sub readDoc
{
	my $id = shift;
	my $doc = get(sprintf('http://mrs.cmbi.ru.nl/mrs-5/download?db=uniprot&id=%s', $id);
	return $doc;
}

sub main()
{
	my $script = MRS::load_script('.', 'uniprot');
	
	my $doc = &readDoc('104k_thepa');
	
	$script->parse();
}

&main();
exit;


package MRS;

our $VERSION = '6.0';

use strict;
#use warnings;
use CGI;
use Data::Dumper;

my %scripts;

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

	my $script_name = "MRS::Script";
	
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

package MRS::Script;

use strict;
use Data::Dumper;
use File::stat;

require Exporter;
require DynaLoader;

our @ISA = qw(Exporter DynaLoader MRS);
our @EXPORT = qw( );

sub new
{
	my $pkg = shift;
#	my $self = MRS::new_MRS_Script(@_);
	my $self = { @_ };
	bless $self, $pkg if defined($self);
}

sub DESTROY
{
	return unless $_[0]->isa('HASH');
	my $self = tied(%{$_[0]});
	return unless defined $self;
	MRS::delete_MRS_Script($self);
}

sub TIEHASH
{
	my ($classname, $obj) = @_;
	return bless $obj, $classname;
}

sub version
{
	my ($self, $version) = @_;
	
	if (defined $version)
	{
		$self->{version} = $version;
	}
	elsif (not defined $self->{version})
	{
		my $date = 0;
		
		foreach my $file ($self->raw_files)
		{
			my $mtime = stat($file)->mtime;
			$date = $mtime if $mtime > $date;
		}
	
		$self->{version} = localtime $date;
	}
	
	return $self->{version};
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

sub index_unique_string
{
	my ($self, $name, $value) = @_;
	
	print "index_unique_string($name, $value)\n";
}

sub store_attribute
{
	my ($self, $name, $value) = @_;
	
	print "store_attribute($name, $value)\n";
}

1;
