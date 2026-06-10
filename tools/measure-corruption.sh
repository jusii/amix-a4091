#!/bin/sh
# measure-corruption.sh  (runs ON the Amix box)
#
# Measures the RATE of the intermittent ld kernel-image corruption (NOTES s18).
# Same per-build procedure as build-clean-kernel.sh, but runs a FIXED number of
# full builds and records EVERY `sum -r relocunix` instead of stopping at the
# first recurrence.  The clean ld output is byte-deterministic (its sum recurs);
# each corruption is unique.  So in the final tally the most-frequent sum is the
# clean kernel and every singleton sum is a corrupt build.
#
# Used to A/B test whether the A4091's presence affects the corruption rate:
# run this booted WITH the A4091 attached and again WITHOUT it, compare rates.
#
# Usage:  sh /root/measure-corruption.sh [N]      (default N=15)
set -u
N=${1:-15}
cd /usr/sys || exit 2
: > /root/mc_sums.txt
echo "measure-corruption: N=$N  start"
i=1
while [ $i -le $N ]; do
  cd /usr/sys/amiga/alien; rm -f a4091.o exp; touch a4091.c
  cd /usr/sys; rm -f relocunix unix OLDrelocunix amiga/exp amiga/config/unix.o master.d/exp
  make > /root/mc_build.log 2>&1
  if [ -f relocunix ]; then
    s=`sum -r relocunix | awk '{print $1}'`
  else
    s=NOFILE
  fi
  echo "build $i: sum=$s"
  echo "$s" >> /root/mc_sums.txt
  i=`expr $i + 1`
done
echo "=== TALLY (count  sum) -- top line = clean (recurring); singletons = corrupt ==="
sort /root/mc_sums.txt | uniq -c | sort -rn
echo "=== DONE measure-corruption N=$N ==="
