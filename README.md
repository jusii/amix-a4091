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
> 🟡 emulation-only / 🔴 corrected) are used throughout [`NOTES.md`](NOTES.md).

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
  ([`src/kernel-patches/support.c`](src/kernel-patches/support.c)), not a hardcoded assumption.
- ✅ **A fix for the kernel "boot-breaker"** — relinking the Amix kernel hits an intermittent (~70%)
  `ld` write corruption that Guru-Meditates the relocator (`D245`). [`../amix-kerntools/tools/build-clean-kernel.sh`](../amix-kerntools/tools/build-clean-kernel.sh)
  defeats it by building until the output checksum is stable. This bug bites *anyone* who relinks the
  Amix kernel, A4091 or not.

The full, dated lab notebook is in [`NOTES.md`](NOTES.md) (§1–§20).

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
  SCSI hardcoded whenever RAM > 7 MB stole card 0 and the root read went to non-existent hardware. See
  [`NOTES.md`](NOTES.md) §18–§19.

---

## Repository layout

| Path | What |
|---|---|
| [`NOTES.md`](NOTES.md) | The living lab notebook — every experiment, dead end and result, tagged ✅/🟡/🔴. |
| [`GRIMOIRE-HANDOFF.md`](GRIMOIRE-HANDOFF.md) | A self-contained brief that folds this work into the [`grimoire-amix`](https://github.com/Jusii/grimoire-amix) knowledge base. |
| [`PROPOSAL.md`](PROPOSAL.md) | Discoveries fed back to grimoire-amix. |
| [`src/a4091-wr.c`](src/a4091-wr.c) | The production 53C710 READ+WRITE driver. |
| [`src/kernel-patches/`](src/kernel-patches/) | The kernel diffs: `sd.c` (registry row), `support.c` (auto-detection), `alien-Makefile`. |
| [`src/`](src/) | Driver variants and on-box helpers (`memread.c`, `makebigfile.c`, …). |
| [`tools/`](tools/) | A4091-specific tooling: `bisect-boot.sh`, `gsio-test.sh`, `measure-corruption.sh`, `parse_rdb.py`. |
| [`driver.conf`](driver.conf) | One-line driver identity consumed by the shared build harness. |
| [`../amix-kerntools/`](../amix-kerntools/) | Shared build hub (split out 2026-06): clean-gate, `checkunix.c`, `relsim.py`, `amixsync.py`, `amishot.sh`, the `build-kernel.sh` harness and the golden build image. |
| `docs/` | Reference notes (e.g. the 53C710 register/SCRIPTS reference). |
| `*.uae` | Amiberry configs for the A3000/ECS, A4000/AGA and build environments. |
| `hdf/`, `tmp/`, `build/`, ROMs | **git-ignored** — proprietary disk images, ROMs and scratch (see Legal). |

---

## Build & run (emulation)

You need a working Amix 2.1 install in **Amiberry**, the A4091 autoboot ROM, and a spare hardfile for
the A4091 disk. (None of these are in this repo — see Legal.)

1. **Reach the box.** Files go over FTP (Amix is NFSv2-only): `../amix-kerntools/tools/amixsync.py push <file>`. Commands
   go over scripted telnet: `../grimoire-amix/tools/host-net/amixsh.py '<cmd>'`. Both default to the
   running box; set `AMIX_PASS`.
2. **Install the driver + patches** into `/usr/sys/amiga/alien/a4091.c`, and apply the three files in
   `src/kernel-patches/` to `sd.c`, `support.c` and the `alien` `Makefile`.
3. **Build a clean kernel** — never trust a single `make`:
   ```sh
   sh /tmp/build-clean-kernel.sh      # relinks until the checksum recurs == the clean kernel
   ```
4. **Install onto a throwaway/clone disk's boot partition** — *never* `make install` onto your working
   disk:
   ```sh
   cd /stand
   ( cat boot1.boot; ./makeiblk boot2.boot; cat boot2.boot; ./makeiblk unix; cat unix ) \
     | dd conv=sync of=/dev/dsk/c8d0s3
   ```
5. **Boot.** Use `amix-a4091-boot-aga.uae` (A4000/AGA + a Kickstart 3.0 A4000 ROM) for the
   root-on-A4091 case, or `amix-dbg.uae` (A3000/ECS) for the A3000 + A4091-coexists case.

The complete procedure, the device-numbering scheme, and every script is documented in
[`GRIMOIRE-HANDOFF.md`](GRIMOIRE-HANDOFF.md).

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

## Relationship to grimoire-amix

This repo is the *hands-on* half of a pair. [`grimoire-amix`](https://github.com/Jusii/grimoire-amix) is
the durable, LLM-friendly knowledge base of how Amix works; this repo is where the A4091 driver was
actually built and proven. [`GRIMOIRE-HANDOFF.md`](GRIMOIRE-HANDOFF.md) is written to be handed to a
documentation agent that folds these results back into the grimoire.

## Legal

Amix, its HDF/ADF disk images, Kickstart ROMs and manuals are **proprietary Commodore software**
(abandonware, not licensed for redistribution) and are **not** included here — the relevant paths are
git-ignored. You must supply your own legally-obtained copies. The A4091 autoboot ROM and the
`ncr53cxxx` SCRIPTS assembler come from the open-source [a4091-software](https://github.com/A4091/a4091-software)
project. The original code in this repository (drivers, patches, tooling) is the author's own work.

## Credits

By Jussi Alanärä. AI tooling — **Claude Code** — was used throughout to investigate, build and verify
this work; the lab notebook records what was tried and what held up. Built on the reverse-engineering and
documentation efforts of the Amiga Unix community and the a4091-software project.
