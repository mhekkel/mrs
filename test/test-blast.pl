#!/usr/bin/perl
# the first line of the script must tell us which language interpreter to use,
# in this case its perl

use strict;
use warnings;
use Data::Dumper;

# import the modules we need for this test; XML::Compile is included on the server
# by default.

use LWP::Simple;

use XML::Compile::WSDL11;
use XML::Compile::SOAP11;
use XML::Compile::Transport::SOAPHTTP;

# a small protein sequence
my $seq = 'TTCCPSIVARSNFNVCRLPGTSEAICATYTGCIIIPGATCPGDY';
my $db = 'pdb';

eval
{	
	# Retreiving and processing the WSDL
	my $wsdl = get('http://localhost:18090/mrsws/blast/wsdl');
	$wsdl  = XML::LibXML->load_xml(string => $wsdl);
	my $proxy = XML::Compile::WSDL11->new($wsdl);
	
	# Generating a request message based on the WSDL
	my $request = $proxy->compileClient('Blast');
	
	my %params = (
		matrix => 'BLOSUM62',
		'wordSize' => 3,
		expect => 10,
		'lowComplexityFilter' => 1,
		gapped => 1,
		'gapOpen' => 11,
		'gapExtend' => 1,
	);
	
	# Calling the service and getting the response
	my ($answer, $trace) = $request->(
		db => $db,
#		mrsBooleanQuery => '',
		query => $seq,
		program => 'blastp',
#		params => \%params,
		'reportLimit' => 25
	);

	# We should get a job id back
	if ( defined $answer ) {
		if (not defined $answer->{parameters}->{jobId})
		{
			print "Unexpected data\n";
			print Dumper $trace;
			exit 2;
		}
	} else {    
		print "Failed\n";
		exit 1;
	}

	$request = $proxy->compileClient('BlastJobStatus', xml_format => 1);
	my $job_id = $answer->{parameters}->{jobId};

	# simply test whether the job was accepted in the queue
	($answer, $trace) = $request->( 'jobId' => $job_id );

	if (not defined $answer or not defined $answer->{parameters}->{status})
	{
		print "Failed\n";
		exit 1;
	}

	if ($answer->{parameters}->{status} eq 'error')
	{
		$request = $proxy->compileClient('BlastJobError');
		$answer = $request->( 'jobId' => $job_id );
		
		print $answer->{parameters}->{error}, "\n";
		exit(1);
	}
	
	# if the status is right, we assume all is OK. Don't bother
	# waiting for the result of the blast job.	
	my $status = $answer->{parameters}->{status};
	
	while ($status eq 'queued' or $status eq 'running')
	{
		print STDERR "$status...\n";
		
		sleep(1);
		
		$answer = $request->( 'jobId' => $job_id );
		$status = $answer->{parameters}->{status};
	}
	
	if ($status eq 'finished') {
		$request = $proxy->compileClient('BlastJobResult');
		($answer, $trace) = $request->( 'jobId' => $job_id );

open(my $log, ">trace.log");
$trace->printRequest($log);
$trace->printResponse($log);
close($log);

		print "passed\n";
		exit(0);
	}
	
	print "Unexpected data\n";
	exit 2;
};

if ($@)
{
	print "Caught an exception: $@\n";
	exit 1;
}
