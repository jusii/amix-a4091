#!/bin/bash
# SPDX-License-Identifier: MIT
# gsio-wtest.sh -- WRITE(10) round-trip on the A4091 disk via the write-capable
# driver: write a pattern with gsio, read it back via gsio AND via the kernel
# block path (dd /dev/rdsk/c8d0s0) to prove it persisted to the disk image.
#
# Requires sibling repos amix-kerntools and grimoire-amix; set AMIX_HOST/USER/PASS
# for your Amix box (override GRIM/KERNTOOLS if the siblings live elsewhere).
set -u
cd "$(dirname "$0")/.." || exit 9          # repo root
: "${AMIX_PASS:?set AMIX_PASS to your Amix box password}"
GRIM="${GRIM:-../grimoire-amix}"
SH="python3 $GRIM/tools/host-net/amixsh.py"
SYNC="python3 ${KERNTOOLS:-../amix-kerntools}/tools/amixsync.py"
g(){ $SH "/root/a4091/gsio $* 2>&1" 2>&1 | sed 's/\r$//' | grep -iE 'okay|status=|first longword'; }

echo "### push + build WRITE-capable gsio"
$SYNC push src/gsio.c /root/a4091/gsio.c 2>&1 | grep -i push
$SH 'cd /root/a4091 && cc -I/usr/sys/amiga/alien -o gsio gsio.c 2>&1 | grep -iv multi-char; ls -l gsio' 2>&1 | sed 's/\r$//' | grep gsio

echo "### block 0 BEFORE write (expect 00000000)"
g 1 0 28 00 00 00 00 00 00 00 01 00
echo "### WRITE(10) block 0  (pattern A0 A1 A2 A3 ...)"
g 1 0 2a 00 00 00 00 00 00 00 01 00
echo "### block 0 AFTER write (expect a0a1a2a3)"
g 1 0 28 00 00 00 00 00 00 00 01 00

echo "### WRITE(10) block 5, then read it back"
g 1 0 2a 00 00 00 00 05 00 00 01 00
g 1 0 28 00 00 00 00 05 00 00 01 00
echo "### block 1 (never written, expect 00000000)"
g 1 0 28 00 00 00 00 01 00 00 01 00

echo "### persistence: read block 0 via the KERNEL block path (dd /dev/rdsk/c8d0s0)"
$SH 'dd if=/dev/rdsk/c8d0s0 bs=512 count=1 of=/tmp/b0 2>/dev/null; od -x /tmp/b0 2>&1 | head -2' 2>&1 | sed 's/\r$//' | grep -vE 'DONE_|amixsh|^======|echo D|^dd |^od '
