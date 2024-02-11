#!/usr/bin/env perl

use strict;
use warnings;

use Cwd;

sub read_link($) {
  my $file = $_[0];
#  $file = `readlink -f $file`;
#  $file = readlink($file) while -l $file;
  $file = Cwd::abs_path($file);
  return $file;
}

# Collect all executables from common folders

my @exes;
for my $exe (`find /bin /usr/bin /snap -type f -executable`) {
  chop $exe;
  $exe = read_link($exe);
  next if `file $exe` !~ /ELF/;
  push @exes, $exe;
}

# Uniq executables

my %seen;
@exes = grep { ! $seen{$_}++; } @exes;

# Collect info about used libs

my %counts;

for my $exe (@exes) {
#  print "$exe\n";
  for (`ldd $exe`) {
    chop;
    if (/[^\s]+ => ([^\s]+)/) {
      my $lib = $1;
      $lib = read_link($lib);
      $counts{$lib}++;
    }
  }
}

# Collect savings

my $total_saving = 0;

for my $lib (sort keys %counts) {
  my $uses = $counts{$lib};
  next if $uses <= 1;

  my $size = -s $lib;
  my $saving = ($uses - 1) * $size;
  $total_saving += $saving;

  print "$lib: $uses uses, $saving savings\n";
}

$total_saving /= 1024 * 1024;
print "Total savings: $total_saving M\n";
