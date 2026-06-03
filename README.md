# Amix A4091 driver project

Writing a device driver for **Amiga UNIX (Amix — Commodore's SVR4 port to 68030 Amigas)** that
drives the **Commodore A4091 SCSI-2 host adapter** — a **Zorro III** board built on the
**NCR/Symbios 53C710** — so Amix can see, and ultimately **boot from**, a disk on an A4091.

This is pioneering work on two fronts at once: Amix is officially **Zorro II only** (its kernel
memory-mapping layer can't address Zorro III space, and that source was never shipped), and Amix
has **no 53C710 driver**. Expect dead ends; move in small verifiable increments.

## Layout
- `NOTES.md` — living working notes (Amix driver model, the Zorro III obstacle, experiments, state). Tagged ✅/🟡/🔴.
- `PROPOSAL.md` — discoveries to feed back to the `grimoire-amix` knowledge base (consume grimoire, produce a proposal).
- `amix-a4091.uae` — my Amiberry config (copy of the reference `amix_net.uae`), boots my own HDF copies.
- `tools/amixsync.py` — laptop↔Amix file bridge over FTP (push/pull files+dirs).
- `src/` — driver and probe source (built natively on the box).
- `hdf/`, `tmp/`, `build/` — git-ignored (disk images, scratch, build output).

## The running box
Amix 2.1 (SVR4.0) in Amiberry 8.1.6 at `AMIX_HOST` (root/`REDACTED`). Host LAN plumbing is up via
the `amix-lan.service`. Reach it: `tools/amixsync.py` (ftp) and `../grimoire-amix/tools/host-net/amixsh.py`
(telnet, scripted). Knowledge base: `../grimoire-amix` (read-only — authoritative for Amix facts).

## Status
Environment stood up; native build loop proven (push → `cc` → run → pull). The 53C710 driver itself
is the work ahead. See `NOTES.md`.
