# Amix on the A4091 — a Zorro III SCSI driver for Amiga UNIX

A native SCSI driver and a universal, auto-detecting kernel that let **Amiga UNIX (Amix —
Commodore's SVR4.0 port to the 68030 Amiga)** drive — and **boot from** — the **Commodore A4091**,
a **Zorro III** SCSI-2 host adapter built on the **NCR/Symbios 53C710**.

Amix shipped with no 53C710 driver and is documented as **Zorro II only**. This project closes both
gaps: the A4091 is now a first-class, auto-detected SCSI controller that coexists with the stock
A2090/A2091/A3000 drivers, and Amix boots with **root, swap and filesystem entirely on an A4091** —
which is what a real **Amiga A4000** needs, since the A4000 has no built-in SCSI at all.

> Status: **proven in emulation (Amiberry) on both an A3000 (ECS) and an A4000 (AGA) profile, from one
> kernel binary.** Real-hardware validation is the remaining open item. Confidence tags (✅ reproduced /
> 🟡 emulation-only) are used below.

---

## What works

- ✅ **A 53C710 block driver for Amix** ([`src/a4091-wr.c`](src/a4091-wr.c)) — table-indirect SCRIPTS,
  one program that does both `READ(10)` and `WRITE(10)` by dispatching on the live SCSI bus phase. The
  A4091 disk is a fully usable Amix filesystem: `mkfs ufs` + `mount`, files persist, end to end.
- ✅ **Boot with root on the A4091** — root (read/write), swap, multi-user, networking, with the A3000's
  own SCSI switched off. Boots to login in ~160 s including fsck.
- ✅ **One universal kernel for both machines** — the same binary auto-detects the hardware: an A3000
  boots on its own WD33C93 SCSI with the A4091 coexisting as a second card, and an A4000 + A4091 boots
  with root on the A4091. Detection is a **chipset gate + WD33C93 probe** in `autocon()`
  ([`src/kernel-patches/support.c.patch`](src/kernel-patches/support.c.patch)), not a hardcoded
  assumption.
- ✅ **A fix for the kernel "boot-breaker"** — relinking the Amix kernel hits an intermittent (~70%)
  `ld` write corruption that Guru-Meditates the relocator (`D245`). The build harness (in the companion
  **amix-kerntools** repo) defeats it by relinking until the output checksum is stable. This bug bites
  *anyone* who relinks the Amix kernel, A4091 or not.

---

## Why it was hard

- **The Zorro III gap.** The A4091 lives at physical `0x40000000`, which falls in the unmapped gap
  between the 68030's two transparent-translation windows — so a stock Zorro II driver simply can't
  reach its registers. The driver page-maps the board with `sptalloc()` instead. DMA still lands in
  low RAM, so bus-mastering is unaffected.
- **No 53C710 driver, no debugger.** The driver is built **natively on the box** (no cross-compiler),
  with a K&R-friendly SVR4 `cc`, no `nm`/`dis`/`dump`, and a big-endian ILP32 ABI.
- **The relocator was a red herring twice over.** The `D245` Guru looked like a poll-loop bug, then a
  relocator bug; it was actually a random file-write corruption. And the "can't boot from A4091" panic
  looked like an early-boot driver problem; it was actually a device-dispatch bug — a *phantom* A3000
  SCSI hardcoded whenever RAM > 7 MB stole card 0 and the root read went to non-existent hardware.

---

## Repository layout

| Path | What |
|---|---|
| [`src/a4091-wr.c`](src/a4091-wr.c) | The production 53C710 READ+WRITE driver. |
| [`src/a4091-detection.c`](src/a4091-detection.c), `src/a4091*.c` | The detection milestone and the incremental driver variants / on-box probes (`memread.c`, `makebigfile.c`, …). |
| [`src/kernel-patches/`](src/kernel-patches/) | The kernel changes as **diffs** against stock Amix: `sd.c.patch` (registry rows + `sdcardbase()`), `support.c.patch` (A3000-vs-A4000 auto-detection), and `alien-Makefile`. |
| [`reference/scripts/`](reference/scripts/) | The NCR 53C710 SCRIPTS assembler source the driver runs (`inq.ss`, `inq-wr.ss`) and their assembled output. |
| [`driver.conf`](driver.conf) | One-line driver identity (A4091/A4092/A4770) consumed by the build harness. |
| [`tools/`](tools/) | Host-side test tooling: `bisect-boot.sh`, `gsio-test.sh`, `gsio-wtest.sh`, `measure-corruption.sh`, `parse_rdb.py`. |
| [`docs/`](docs/) | The 53C710 register / SCRIPTS reference. |
| `*.uae` | Sample Amiberry configs for the A3000/ECS, A4000/AGA and build environments (edit the placeholder paths). |
| `hdf/`, `tmp/`, `build/`, ROMs | **git-ignored** — proprietary disk images, ROMs and scratch (see Legal). |

The build harness, clean-gate, the FTP/telnet bridges and the golden build image live in the companion
**[amix-kerntools](https://github.com/Jusii/amix-kerntools)** repo; the host network bridge lives in
**[grimoire-amix](https://github.com/Jusii/grimoire-amix)**. Clone them as siblings of this repo.

---

## Build & run (emulation)

You need a working Amix 2.1 install in **Amiberry**, the A4091 autoboot ROM, and a spare hardfile for
the A4091 disk. (None of these are in this repo — see Legal.) The companion `amix-kerntools` and
`grimoire-amix` repos provide the harness; `set AMIX_HOST`/`AMIX_USER`/`AMIX_PASS` for your box.

1. **Reach the box.** Files go over FTP (Amix is NFSv2-only):
   `../amix-kerntools/tools/amixsync.py push <file>`. Commands go over scripted telnet:
   `../grimoire-amix/tools/host-net/amixsh.py '<cmd>'`.
2. **Install the driver + patches** into your Amix source tree:
   - Push the driver as the `alien` source:
     `amixsync.py push src/a4091-wr.c /usr/sys/amiga/alien/a4091.c`
   - Apply the two kernel patches **once** against your stock Amix tree (Amix 2.1 is a single frozen
     version, so they apply cleanly):
     ```sh
     cd /usr/sys
     patch -p1 < sd.c.patch          # amiga/alien/sd.c   : A4091/A4092/A4770 rows + sdcardbase()
     patch -p1 < support.c.patch     # amiga/kernel/support.c : A3000-vs-A4000 auto-detection
     ```
   - Add `a4091.o` to the `alien` Makefile (see [`src/kernel-patches/alien-Makefile`](src/kernel-patches/alien-Makefile)).
   - (The `amix-kerntools` harness automates push + the `sd.c`/Makefile generation from `driver.conf`;
     the manual `patch` steps above are the self-contained equivalent.)
3. **Build a clean kernel** — never trust a single `make`; the `amix-kerntools` clean-gate relinks until
   the output checksum recurs (== the clean kernel) to defeat the `D245` boot-breaker.
4. **Install onto a throwaway/clone disk's boot partition** — *never* `make install` onto your working
   disk:
   ```sh
   cd /stand
   ( cat boot1.boot; ./makeiblk boot2.boot; cat boot2.boot; ./makeiblk unix; cat unix ) \
     | dd conv=sync of=/dev/dsk/c8d0s3
   ```
5. **Boot.** Use `amix-a4091-boot-aga.uae` (A4000/AGA + a Kickstart 3.0 A4000 ROM) for the
   root-on-A4091 case, or `amix-dbg.uae` (A3000/ECS) for the A3000 + A4091-coexists case. (Edit the
   placeholder paths in the `.uae` first.)

> ⚠️ A corrupt kernel written to a boot partition bricks that disk. Always clean-gate the build, keep a
> backup HDF, and install onto a clone. The working disk is never touched by the dev loop.

---

## Status

| | Emulation (Amiberry) | Real hardware |
|---|---|---|
| A4091 disk read/write, UFS filesystem | ✅ | 🟡 pending |
| Boot with root on the A4091 (A4000-style) | ✅ | 🟡 pending |
| A3000 + A4091 coexisting (root on A3000) | ✅ | 🟡 pending |
| One universal auto-detecting kernel | ✅ | 🟡 pending |

The chipset gate reads VPOSR (always-present, bus-safe) and skips the A3000 SCSI probe entirely on an
A4000, so the logic is sound on metal in principle — but the WD33C93 probe and the completion timing for
a *physical* (non-instant) disk can only be fully trusted once run on a real A3000 and a real
A4000 + A4091.

---

## Legal

Amix, its HDF/ADF disk images, Kickstart ROMs and manuals are **proprietary Commodore software**
(abandonware, not licensed for redistribution) and are **not** included here — the relevant paths are
git-ignored. You must supply your own legally-obtained copies. The kernel changes are shipped as
**patches** against the stock Amix source, so this repo contains only the original contribution, not
Commodore/AT&T source. The A4091 autoboot ROM and the `ncr53cxxx` SCRIPTS assembler come from the
open-source [a4091-software](https://github.com/A4091/a4091-software) project.

The original code in this repository (driver, patches, tooling, SCRIPTS) is the author's own work and is
released under the **MIT License** — see [`LICENSE`](LICENSE).

## Credits

By Jussi Alanärä. AI tooling — **Claude Code** — was used throughout to investigate, build and verify
this work. Built on the reverse-engineering and documentation efforts of the Amiga Unix community and
the a4091-software project.
