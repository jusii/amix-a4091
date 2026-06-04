#!/bin/bash
# amishot.sh [outfile] -- capture the running Amiberry's Amiga framebuffer to a PNG
# via Amiberry's IPC socket.  Works under Wayland/GNOME where grim/gnome-screenshot
# /Shell-DBus are all blocked, because it asks Amiberry itself to render the shot.
# Protocol (discovered from the amiberry binary): send "SCREENSHOT\t<abspath>\n"
# to /run/user/<uid>/amiberry.sock; reply "OK\t<path>" or "ERROR\t<msg>".
F="${1:-$(pwd)/tmp/shots/shot.png}"
case "$F" in /*) ;; *) F="$(pwd)/$F";; esac
mkdir -p "$(dirname "$F")"
SOCK="/run/user/$(id -u)/amiberry.sock"
python3 - "$F" "$SOCK" <<'PY'
import socket, sys
f, sock = sys.argv[1], sys.argv[2]
try:
    s = socket.socket(socket.AF_UNIX); s.settimeout(3); s.connect(sock)
    s.sendall(('SCREENSHOT\t%s\n' % f).encode())
    print(s.recv(256).decode().strip())
    s.close()
except Exception as e:
    print("ERR", e)
PY
