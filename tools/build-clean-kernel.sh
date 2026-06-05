#!/bin/sh
# build-clean-kernel.sh  (runs ON the Amix box)
#
# Works around the intermittent ld kernel-image corruption (the "D245
# boot-breaker", NOTES s18): ~70% of `make` runs shift one ~8KB block of the
# linked kernel by 8 bytes, randomly placed.  The CLEAN ld output is
# byte-deterministic; each corruption is unique.  So: rebuild (link-only) until
# a `sum` value RECURS -- the recurring value is the deterministic clean kernel.
# Then relocunix is guaranteed good and `make install` is safe.
#
# Usage:  sh /tmp/build-clean-kernel.sh   ->  leaves a verified-clean relocunix
#         exit 0 = clean relocunix ready; 1 = could not stabilize.
set -u
cd /usr/sys || exit 2
: > /tmp/bck_seen.txt
n=1
while [ $n -le 25 ]; do
  cd /usr/sys/amiga/alien; rm -f a4091.o exp; touch a4091.c
  cd /usr/sys; rm -f relocunix unix OLDrelocunix amiga/exp amiga/config/unix.o master.d/exp
  make > /tmp/bck_build.log 2>&1
  s=`sum -r relocunix | awk '{print $1}'`
  echo "build $n: sum=$s"
  if grep "^$s\$" /tmp/bck_seen.txt > /dev/null 2>&1; then
    # sum reproduced => deterministic clean output. Confirm symtab too.
    /root/checkunix relocunix > /tmp/bck_chk.log 2>&1
    echo "STABLE after $n builds: clean relocunix (sum=$s)"
    cat /tmp/bck_chk.log
    exit 0
  fi
  echo "$s" >> /tmp/bck_seen.txt
  n=`expr $n + 1`
done
echo "FAILED: relocunix did not stabilize in 25 builds"
exit 1
