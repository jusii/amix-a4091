#!/usr/bin/env python3
"""amixsync.py -- laptop <-> Amix file bridge over FTP.

Amix 2.1 (SVR4.0) is an NFSv2-only NFS client and the host's kernel nfsd can't
serve NFSv2, so we move files over the box's ftpd instead. Binary transfers,
recursive directories, remote dirs auto-created.

Usage:
  amixsync.py push <local> <remote>     # upload file or dir (recursive)
  amixsync.py pull <remote> <local>     # download file or dir (recursive)
  amixsync.py ls   <remote>             # list a remote dir
  amixsync.py rm   <remote>             # delete a remote file

Examples:
  amixsync.py push ./src /tmp/a4091           # push a source tree to Amix
  amixsync.py pull /tmp/a4091/siop.o ./build/ # pull a built object back
  amixsync.py pull /usr/lib/fs/nfs /tmp/grab  # pull a whole remote dir

Target/creds via env (defaults shown):
  AMIX_HOST=AMIX_HOST  AMIX_PORT=21  AMIX_USER=root  AMIX_PASS=REDACTED
"""
import ftplib, os, sys

HOST = os.environ.get("AMIX_HOST", "AMIX_HOST")
PORT = int(os.environ.get("AMIX_PORT", "21"))
USER = os.environ.get("AMIX_USER", "root")
PASS = os.environ.get("AMIX_PASS", "REDACTED")


def connect():
    f = ftplib.FTP()
    f.connect(HOST, PORT, timeout=30)
    f.login(USER, PASS)
    f.voidcmd("TYPE I")  # binary
    return f


def is_remote_dir(f, path):
    """True if `path` is a remote directory (probe via CWD, restore cwd)."""
    cur = f.pwd()
    try:
        f.cwd(path)
        f.cwd(cur)
        return True
    except ftplib.error_perm:
        return False


def ensure_remote_dir(f, path):
    """mkdir -p for the remote side. `path` is a directory path."""
    parts = [p for p in path.split("/") if p]
    cur = "/" if path.startswith("/") else ""
    for p in parts:
        cur = (cur.rstrip("/") + "/" + p) if cur else p
        try:
            f.mkd(cur)
        except ftplib.error_perm:
            pass  # already exists (or no perm -- STOR will report the real error)


def push_file(f, local, remote):
    parent = os.path.dirname(remote.rstrip("/"))
    if parent:
        ensure_remote_dir(f, parent)
    with open(local, "rb") as fh:
        f.storbinary("STOR " + remote, fh)
    print("  push %s -> %s (%d bytes)" % (local, remote, os.path.getsize(local)))


def push_dir(f, localdir, remotedir):
    ensure_remote_dir(f, remotedir)
    for root, dirs, files in os.walk(localdir):
        rel = os.path.relpath(root, localdir)
        rdir = remotedir if rel == "." else remotedir.rstrip("/") + "/" + rel.replace(os.sep, "/")
        ensure_remote_dir(f, rdir)
        for name in files:
            push_file(f, os.path.join(root, name), rdir.rstrip("/") + "/" + name)


def pull_file(f, remote, local):
    if os.path.isdir(local) or local.endswith("/"):
        local = os.path.join(local, os.path.basename(remote.rstrip("/")))
    d = os.path.dirname(local)
    if d:
        os.makedirs(d, exist_ok=True)
    with open(local, "wb") as fh:
        f.retrbinary("RETR " + remote, fh.write)
    print("  pull %s -> %s (%d bytes)" % (remote, local, os.path.getsize(local)))


def remote_names(f, path):
    """Entry names directly under remote dir `path` (NLST, basenames only)."""
    cur = f.pwd()
    f.cwd(path)
    try:
        names = f.nlst()
    finally:
        f.cwd(cur)
    out = []
    for n in names:
        b = n.rsplit("/", 1)[-1]
        if b not in (".", ".."):
            out.append(b)
    return out


def pull_dir(f, remotedir, localdir):
    base = os.path.join(localdir, os.path.basename(remotedir.rstrip("/")))
    os.makedirs(base, exist_ok=True)
    for name in remote_names(f, remotedir):
        rpath = remotedir.rstrip("/") + "/" + name
        if is_remote_dir(f, rpath):
            pull_dir(f, rpath, base)
        else:
            pull_file(f, rpath, os.path.join(base, name))


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    cmd = sys.argv[1]
    f = connect()
    try:
        if cmd == "push":
            local, remote = sys.argv[2], sys.argv[3]
            if os.path.isdir(local):
                push_dir(f, local.rstrip("/"), remote)
            else:
                if remote.endswith("/") or is_remote_dir(f, remote):
                    remote = remote.rstrip("/") + "/" + os.path.basename(local)
                push_file(f, local, remote)
        elif cmd == "pull":
            remote, local = sys.argv[2], sys.argv[3]
            if is_remote_dir(f, remote):
                pull_dir(f, remote, local.rstrip("/") or ".")
            else:
                pull_file(f, remote, local)
        elif cmd == "ls":
            for n in sorted(remote_names(f, sys.argv[2])):
                print(n)
        elif cmd == "rm":
            f.delete(sys.argv[2])
            print("  rm %s" % sys.argv[2])
        else:
            print("unknown command: %s" % cmd)
            print(__doc__)
            sys.exit(2)
    finally:
        try:
            f.quit()
        except Exception:
            pass


if __name__ == "__main__":
    main()
