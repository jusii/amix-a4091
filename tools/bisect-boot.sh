#!/bin/bash
# bisect-boot.sh <src-file>
# Push a bisection candidate as a4091.c, clean-rebuild + install, then cold-boot
# and gsio-test it.  Transport-safe (no nested sh -c), with a freshness gate so a
# stale kernel cannot masquerade as a pass.  Prints a single RESULT: line.
set -u
SRC="$1"
cd ~/Devel/Omat/Amiga/Amix/A4091 || exit 9
export AMIX_PASS=REDACTED
GRIM=../grimoire-amix
SH="python3 $GRIM/tools/host-net/amixsh.py"
HOST=AMIX_HOST
UAE="$PWD/amix-a4091.uae"

tn(){ timeout 2 bash -c "</dev/tcp/$HOST/23" 2>/dev/null; }
up_wait(){ local t=$1 s=$(date +%s); while [ $(($(date +%s)-s)) -lt $t ]; do tn && { echo $(($(date +%s)-s)); return 0; }; sleep 5; done; return 1; }
down_wait(){ local i; for i in $(seq 1 50); do tn || return 0; sleep 3; done; }
coldboot(){ pkill -x amiberry 2>/dev/null; sleep 3; pgrep -x amiberry >/dev/null && { pkill -9 -x amiberry; sleep 2; }; nohup amiberry -f "$UAE" -G > tmp/amiberry-verbose.log 2>&1 & sleep 4; }

echo "### [1] ensure box up"
tn || coldboot
T=$(up_wait 300) || { echo "RESULT: FATAL box wont come up at all"; exit 1; }
echo "box up (${T}s)"

echo "### [2] push $SRC -> a4091.c"
python3 tools/amixsync.py push "$SRC" /usr/sys/amiga/alien/a4091.c 2>&1 | grep -i push

echo "### [3] clean rebuild + install (background; trailing echo avoids '& ;' syntax error from amixsh sentinel)"
$SH 'cd /usr/sys/amiga/alien && rm -f a4091.o exp; cd /usr/sys && rm -f relocunix unix amiga/exp amiga/config/unix.o; nohup make install > /tmp/inst.log 2>&1 & echo BG_PID_$!' 2>&1 | grep -i BG_PID_

echo "### [4] wait for build+install"
s=$(date +%s); st=0; ERR=""
while [ $(($(date +%s)-s)) -lt 460 ]; do
  L=$($SH 'tail -4 /tmp/inst.log 2>/dev/null' 2>/dev/null | sed 's/\r$//')
  echo "$L" | grep -q "records out" && { st=1; break; }
  echo "$L" | grep -qiE "error|cannot open|undefined|conflict" && { st=2; ERR="$L"; break; }
  sleep 12
done
echo "build wait ${st} after $(($(date +%s)-s))s"

echo "### [5] freshness gate"
SZ=$($SH 'cd /usr/sys/amiga/alien; size a4091.o' 2>/dev/null | sed 's/\r$//' | grep -E '^[0-9]' | head -1)
CC=$($SH 'grep -c a4091.c /tmp/inst.log' 2>/dev/null | sed 's/\r$//' | grep -oE '^[0-9]+' | head -1)
echo "a4091.o: [$SZ]  a4091.c-compile-lines: ${CC:-?}"
[ "$st" = 2 ] && { echo "RESULT: BUILD-ERROR $ERR"; exit 2; }
[ "${CC:-0}" = 0 ] && { echo "RESULT: STALE-BUILD (a4091.c not recompiled) -- NOT booting"; exit 3; }

echo "### [6] cold boot the freshly-built kernel"
$SH 'sync;sync;sync; nohup /usr/sbin/shutdown -y -g0 -i0 </dev/null >/dev/null 2>&1 &' >/dev/null 2>&1
down_wait; sleep 26
coldboot
if T=$(up_wait 330); then
  echo "RESULT: BOOT-UP (${T}s) -- this variant BOOTS"
  echo "### [7] gsio 1 0"
  $SH '/root/a4091/gsio 1 0 2>&1' 2>&1 | grep -iv '^#'
else
  echo "RESULT: BOOT-FAIL (Guru/green-screen) -- this variant's code BREAKS the boot"
fi
