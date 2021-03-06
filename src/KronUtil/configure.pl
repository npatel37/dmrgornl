#!/usr/bin/perl
=pod
Copyright (c) 2009-2015, UT-Battelle, LLC
All rights reserved

[DMRG++, Version 4.0]

*********************************************************
THE SOFTWARE IS SUPPLIED BY THE COPYRIGHT HOLDERS AND
CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED.

Please see full open source license included in file LICENSE.
*********************************************************

=cut
use warnings;
use strict;

use lib "../../../PsimagLite/scripts";
use Make;

my ($gccdash) = @ARGV;
defined($gccdash) or $gccdash = "";

my @names = ("KronUtil", "util", "utilComplex", "csc_nnz");

my @drivers;
my $dotos = "";
my $total= scalar(@names);
for (my $i = 0; $i < $total; ++$i) {
	my $name = $names[$i];
	my %dmrgDriver = (name => $name, aux => 1);
	push @drivers,\%dmrgDriver;
	$dotos .= " $name.o ";
}

createMakefile($gccdash);

sub createMakefile
{
	my ($gccdash) = @_;
	Make::backupMakefile();

	my $fh;
	open($fh, ">", "Makefile") or die "Cannot open Makefile for writing: $!\n";

	my %args;
	$args{"code"} = "KronUtil";
	$args{"additional3"} = "libkronutil.a test1 test2";
	$args{"path"} = "../";
	Make::newMake($fh,\@drivers,\%args);

	local *FH = $fh;
print FH<<EOF;

libkronutil.a: $dotos ../Config.make Makefile
	${gccdash}ar rc libkronutil.a $dotos
	${gccdash}ranlib libkronutil.a

test1: libkronutil.a test1.o $dotos ../Config.make
	\$(CXX) \$(CFLAGS) -o test1 test1.o libkronutil.a \$(LDFLAGS)

test2: libkronutil.a test2.o ../Config.make
	\$(CXX) \$(CFLAGS) -o test2 test2.o libkronutil.a \$(LDFLAGS)

EOF

	close($fh);
	print STDERR "$0: File Makefile has been written\n";
}

