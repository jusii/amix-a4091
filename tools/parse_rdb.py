#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Parse an Amiga Rigid Disk Block (RDB) from a raw disk image / HDF.
Lists partitions: name, flags (bootable), bootpri, dostype, LowCyl..HighCyl,
byte offset + size, and the kernel's scsicard signature if it looks like a
boot partition. All RDB values are big-endian."""
import sys, struct

def be32(b, o): return struct.unpack(">I", b[o:o+4])[0]
def be32s(b, o): return struct.unpack(">i", b[o:o+4])[0]

def main():
    path = sys.argv[1]
    f = open(path, "rb")
    # find RDSK in first 16 blocks
    rdb = None; rdbblk = None
    for blk in range(16):
        f.seek(blk*512); b = f.read(512)
        if b[0:4] == b"RDSK":
            rdb = b; rdbblk = blk; break
    if rdb is None:
        print("No RDSK block found in first 16 blocks"); return
    bsize   = be32(rdb, 16)
    cyls    = be32(rdb, 64)
    sectors = be32(rdb, 68)
    heads   = be32(rdb, 72)
    partlist= be32s(rdb, 28)
    print(f"RDSK @blk{rdbblk}  block_size={bsize}  geometry C/H/S={cyls}/{heads}/{sectors}  partlist=blk{partlist}")
    bpc = heads * sectors  # blocks per cylinder
    print(f"blocks/cylinder = {bpc}")
    print()

    nxt = partlist; idx = 0
    while nxt != -1 and nxt != 0xffffffff and nxt > 0:
        f.seek(nxt*512); pb = f.read(512)
        if pb[0:4] != b"PART":
            print(f"  blk{nxt}: not a PART block ({pb[0:4]!r}), stop"); break
        flags = be32(pb, 20)
        namelen = pb[36]
        name = pb[37:37+namelen].decode("latin1", "replace")
        env = 128
        de_surfaces = be32(pb, env + 3*4)
        de_bpt      = be32(pb, env + 5*4)
        de_reserved = be32(pb, env + 6*4)
        de_lowcyl   = be32(pb, env + 9*4)
        de_highcyl  = be32(pb, env + 10*4)
        de_bootpri  = be32s(pb, env + 15*4)
        de_dostype  = be32(pb, env + 16*4)
        bpc_p = de_surfaces * de_bpt
        startblk = de_lowcyl * bpc_p
        nblk = (de_highcyl - de_lowcyl + 1) * bpc_p
        boot = "BOOTABLE" if (flags & 1) else "        "
        dt = de_dostype
        dtstr = bytes([(dt>>24)&0xff,(dt>>16)&0xff,(dt>>8)&0xff,dt&0xff])
        print(f"  s{idx}: name={name:8s} {boot} bootpri={de_bootpri:5d} dostype=0x{dt:08x} ({dtstr!r})")
        print(f"       cyl {de_lowcyl}..{de_highcyl}  surf={de_surfaces} bpt={de_bpt} resv={de_reserved}")
        print(f"       startblk={startblk} (byte 0x{startblk*512:x})  size={nblk} blks ({nblk*512//1024} KB, {nblk*512//1048576} MB)")
        # peek: if this partition holds a raw kernel (boot1 'DOS' or scsicard), show signature
        f.seek(startblk*512); head = f.read(16)
        f.seek(startblk*512 + 10240 + 896444); sc = f.read(4)   # scsicard row0 if raw kernel at 0x2800
        f.seek(startblk*512 + 10240 + 896480); sc3 = f.read(4)  # scsicard row3
        print(f"       head[0:4]={head[0:4]!r}  scsicard.row0={sc.hex() if len(sc)==4 else '--'} row3={sc3.hex() if len(sc3)==4 else '--'}")
        print()
        idx += 1
        nxt = be32s(pb, 16)
        if idx > 16: break

if __name__ == "__main__":
    main()
