#!/bin/bash
# SPDX-License-Identifier: MIT
# gsio-test.sh -- push+build the updated gsio tool on the box, then exercise the
# generic a4091queue: INQUIRY regression, then READ(10) of blocks off the A4091 disk.
#
# Requires sibling repos amix-kerntools (build/sync harness) and grimoire-amix
# (host-net bridge). Set AMIX_HOST/AMIX_USER/AMIX_PASS for your Amix box;
# override GRIM/KERNTOOLS if the siblings live elsewhere.
set -u
cd "$(dirname "$0")/.." || exit 9          # repo root
: "${AMIX_PASS:?set AMIX_PASS to your Amix box password}"
GRIM="${GRIM:-../grimoire-amix}"
SH="python3 $GRIM/tools/host-net/amixsh.py"
SYNC="python3 ${KERNTOOLS:-../amix-kerntools}/tools/amixsync.py"
clean(){ sed 's/\r$//' | grep -vE 'DONE_|amixsh|^======|echo D'; }

echo "### push + build updated gsio"
$SYNC push src/gsio.c /root/a4091/gsio.c 2>&1 | grep -i push
$SH 'cd /root/a4091 && cc -I/usr/sys/amiga/alien -o gsio gsio.c 2>&1; ls -l gsio' 2>&1 | clean

echo "### INQUIRY regression (generic driver, card 1)"
$SH '/root/a4091/gsio 1 0 2>&1' 2>&1 | clean | grep -iE 'okay|INQUIRY|status='

echo "### READ(10) block 0  (cdb 28 00 00000000 00 0001 00 -> 1 block @ LBA0)"
$SH '/root/a4091/gsio 1 0 28 00 00 00 00 00 00 00 01 00 2>&1' 2>&1 | clean

echo "### READ(10) block 1"
$SH '/root/a4091/gsio 1 0 28 00 00 00 00 01 00 00 01 00 2>&1' 2>&1 | clean | grep -iE 'okay|status=|first longword'

echo "### READ(10) 2 blocks @ LBA0 (1024 bytes -- tests larger DMA + nopoll)"
$SH '/root/a4091/gsio 1 0 28 00 00 00 00 00 00 00 02 00 2>&1' 2>&1 | clean | grep -iE 'okay|status=|nbyte|first longword'
