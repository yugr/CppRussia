#!/usr/bin/env perl

# A simple script to benchmark startup time of various applications.
# Run it like
#   $ LD_BIND_NOW ./benchmark.pl 1000 clang -h

use strict;
use warnings;

use Time::HiRes;

# Extract frequency from /proc/cpuinfo

open FILE, '/proc/cpuinfo' or die "Failed to open /proc/cpuinfo";
my $F;
while (<FILE>) {
  if (/cpu MHz.*: (.*)/) {
    $F = $1 * 1000000;
    last;
  }
}
defined $F or die "Failed to extract frequency from /proc/cpuinfo";
close FILE;

# Run test N times

my $N = shift @ARGV;
my $cmd = "@ARGV 2>&1";
#my $cmd = "@ARGV 2>&1 >/dev/null";  WHY THIS DOES NOT WORK ?!
$ENV{LD_DEBUG} = 'statistics';

my $total_runtime = 0;
my $total_loader_time = 0;
my $total_loader_cycles = 0;
for (my $i = 0; $i < $N; ++$i) {
  my $tic = Time::HiRes::gettimeofday();
  my @lines = `$cmd`;
  my $toc = Time::HiRes::gettimeofday();
  $total_runtime += log($toc - $tic);

  my $loader_cycles;
  for (@lines) {
    $loader_cycles = $1 if /total startup time in dynamic loader: ([0-9]+) cycles/;
  }
  defined $loader_cycles or die "Failed to extract loader time";
  $total_loader_time += log($loader_cycles / $F);
  $total_loader_cycles += log($loader_cycles);
}

# Print results

my $gm = exp($total_runtime / $N);
printf("Runtime geomean: %g ms\n", $gm * 1000);

my $gm_loader = exp($total_loader_time / $N);
my $gm_loader_cycles = exp($total_loader_cycles / $N);
printf("Loader geomean: %g ms (%d cycles)\n", $gm_loader, $gm_loader_cycles);
