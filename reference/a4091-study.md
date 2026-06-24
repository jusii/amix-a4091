

# ===== REGISTER_MAP =====

I have everything needed. Here is the deliverable.

---

# 53C710 Register Map for the Amix A4091 Detection Driver

## CRITICAL FRAMING CORRECTION (read first)

The A4091 chip is a **53C710**, and in `a4091-software` it is built with `-DARCH_710 -DNCR53C710=1` (`Makefile:20`). This has two consequences that change the answers:

1. **The authoritative driver is `siop.c`, NOT `siop2.c`.** `siop2.c` is the 53C**720/770** driver (`siop2.c:67` "53C720/770", built with `-DARCH_770` for A4000T/A4770). The two chips have **different register struct layouts** in `siopreg.h` (the `#if defined(ARCH_710)` block at lines 45-104 vs. the `ARCH_720/770` block at lines 106-245). Using siop2.c's offsets for a 710 would be wrong. Everything below is from the **ARCH_710** layout.

2. **The real byte-lane / write-shadow handling lives in `attach.c` and `ncr7xx.c`, not in siop2.c.** siop2.c just uses a `siop_regmap_p rp` struct pointer with no swapping and no +0x40 — it assumes the struct already maps onto the board. The actual A4091-specific access logic is in `ncr7xx.c` (the AmigaOS bring-up/diagnostic) and `attach.c`. The good news: your driver's byte offsets and CTEST8 revision read are **correct**; I confirm them below with file:line references and flag two things to fix.

---

## 1. Authoritative 53C710 register offset table (from `siopreg.h`, ARCH_710 block, lines 45-104)

The 710 register file is a **flat 0x00-0x3f byte map**. Offsets confirmed against `ncr7xx.c:76-115` (`REG_*` defines) which are identical.

| Off | Name | Size | R/W | Meaning | Detection/init use |
|-----|------|------|-----|---------|-----|
| 0x00 | SIEN | 1 | rw | SCSI Interrupt Enable | siopreg.h:47 — write 0 to mask ints during probe |
| 0x01 | SDID | 1 | rw | SCSI Destination ID | siopreg.h:48 |
| 0x02 | SCNTL1 | 1 | rw | SCSI control 1 (RST=0x08 asserts bus reset) | siopreg.h:49 |
| 0x03 | SCNTL0 | 1 | rw | SCSI control 0 (ARB/START/WATN…) | siopreg.h:50 |
| 0x04 | SOCL | 1 | rw | SCSI Output Control Latch | siopreg.h:52 |
| 0x05 | SODL | 1 | rw | SCSI Output Data Latch | siopreg.h:53 |
| 0x06 | SXFER | 1 | rw | SCSI Transfer (sync period/offset) | siopreg.h:54 |
| 0x07 | SCID | 1 | rw | SCSI Chip ID | siopreg.h:55 |
| 0x08 | SBCL | 1 | **ro** | SCSI Bus Control Lines (phase) | siopreg.h:57 |
| 0x09 | SBDL | 1 | ro | SCSI Bus Data Lines (termination check) | siopreg.h:58 |
| 0x0a | SIDL | 1 | ro | SCSI Input Data Latch | siopreg.h:59 |
| 0x0b | SFBR | 1 | ro | SCSI First Byte Received | siopreg.h:60 |
| 0x0c | SSTAT2 | 1 | ro | SCSI status 2 (FIFO flags, phase) | siopreg.h:62 |
| 0x0d | SSTAT1 | 1 | ro | SCSI status 1 | siopreg.h:63 |
| 0x0e | SSTAT0 | 1 | ro | SCSI status 0 | siopreg.h:64 |
| 0x0f | DSTAT | 1 | **ro** | DMA status | siopreg.h:65 |
| 0x10 | DSA | 4 | rw | Data Structure Address | siopreg.h:67 |
| 0x14 | CTEST3 | 1 | ro | Chip test 3 | siopreg.h:69 |
| 0x15 | CTEST2 | 1 | ro | Chip test 2 | siopreg.h:70 |
| 0x16 | CTEST1 | 1 | ro | Chip test 1 (FIFO empty/full nibbles) | siopreg.h:71 |
| 0x17 | CTEST0 | 1 | ro | Chip test 0 | siopreg.h:72 |
| 0x18 | CTEST7 | 1 | rw | Chip test 7 (CDIS burst-disable, DIFF) | siopreg.h:74 |
| 0x19 | CTEST6 | 1 | rw | Chip test 6 (DMA FIFO data) | siopreg.h:75 |
| 0x1a | CTEST5 | 1 | rw | Chip test 5 | siopreg.h:76 |
| 0x1b | CTEST4 | 1 | rw | Chip test 4 (FIFO byte-lane sel) | siopreg.h:77 |
| 0x1c | TEMP | 4 | rw | Temporary Stack reg | siopreg.h:79 — **used by a4091_validate scratch test** |
| 0x20 | LCRC | 1 | rw | LCRC value | siopreg.h:81 |
| **0x21** | **CTEST8** | 1 | rw | Chip test 8 — **high nibble = chip revision**; FLF/CLF FIFO ctl | siopreg.h:82, 593-597 |
| 0x22 | ISTAT | 1 | rw | Interrupt Status (ABRT/RST/SIGP/CON/SIP/DIP) | siopreg.h:83, 574-589 |
| 0x23 | DFIFO | 1 | rw | DMA FIFO | siopreg.h:84 |
| 0x24 | DCMD | 1 | rw | DMA Command | siopreg.h:86 |
| 0x25-0x27 | DBC2/1/0 | 1 ea | rw | DMA Byte Counter (24-bit) | siopreg.h:87-89 |
| 0x28 | DNAD | 4 | rw | DMA Next Address | siopreg.h:91 |
| 0x2c | DSP | 4 | rw | DMA SCRIPTS Pointer | siopreg.h:93 |
| 0x30 | DSPS | 4 | rw | DMA SCRIPTS Pointer Save | siopreg.h:95 |
| 0x34 | SCRATCH | 4 | rw | Scratch Register | siopreg.h:97 — **used by a4091_validate scratch test** |
| 0x38 | DCNTL | 1 | rw | DMA Control (CF clock divider, STD start) | siopreg.h:99, 623-643 |
| 0x39 | DWT | 1 | rw | DMA Watchdog Timer | siopreg.h:100 |
| 0x3a | DIEN | 1 | rw | DMA Interrupt Enable | siopreg.h:101, 610-619 |
| 0x3b | DMODE | 1 | rw | DMA Mode (burst length, FC) | siopreg.h:102, 601-606 |
| 0x3c | ADDER | 4 | — | (internal adder output) | siopreg.h:104 |

Note: on the 710, **SIEN/SSTAT0/SSTAT1/SSTAT2/DSTAT are 8-bit**. On the 720/770 the SCSI-interrupt registers move to 0x40-0x42 and become 16-bit (SIST/SIEN), which is why siop2.c does `rp->siop_sist` as a `u_short` — that does **not** apply to your 710.

---

## 2. Big-endian byte lanes + WRITE-shadow (+0x40) handling — exact code

`siopreg.h` itself contains **no** swap and **no** +0x40 for the bare 710 struct; the board-specific handling is explicit in two places:

### Write shadow = +0x40 for the A4091 (8-bit registers)
`ncr7xx.c:153-154`:
```c
#define NCR_WRITE_SHADOW_710  0x40
#define NCR_WRITE_SHADOW_72X  0x80
```
`ncr7xx.c:300`: `static uint8_t ncr_write_shadow_offset = NCR_WRITE_SHADOW_710;`

All writes go through the shadow; all reads use the primary offset:
```c
/* ncr7xx.c:949-955  -- "Write via the board-specific shadow window" */
static void set_ncrreg8(uint reg, uint8_t value) {
    *ADDR8(a4091_reg_base + ncr_write_shadow_offset + reg) = value;   // reg+0x40
}
/* ncr7xx.c:895-902 -- read from primary */
static uint8_t get_ncrreg8(uint reg) {
    uint8_t value = *ADDR8(a4091_reg_base + reg);                     // reg+0x00
    ...
}
```
`attach.c:531-535` confirms +0x40 is A4091-specific:
```c
#if defined(DRIVER_A4091) || defined(DRIVER_A4092)
    siop_regmap_p rp_write = (siop_regmap_p)((char *)rp + 0x40);
#elif ... A4770/A4000T/A4000T770
    siop_regmap_p rp_write = (siop_regmap_p)((char *)rp + 0x80);
#endif
```
And `a4091_validate()` reads scratch/temp from `rp` but writes them through `rp_write` (`attach.c:544-545, 565-566`).

### 8/32-bit only, NEVER 16-bit — CONFIRMED
- **8-bit:** `ADDR8` (`port.h:30`).
- **32-bit:** `ADDR32` via a `__may_alias__` typedef so the compiler emits a real aligned `move.l` (`port.h:26,31`):
  ```c
  typedef uint32_t __attribute__((__may_alias__)) aliased_uint32_t;
  #define ADDR32(x) (volatile aliased_uint32_t *)(x)
  ```
- **16-bit is never a single bus cycle.** Every 16-bit register access is decomposed into **two byte accesses**, big-endian order (high byte first):
  ```c
  /* ncr7xx.c:904-912 read */
  uint16_t value = ((uint16_t)*ADDR8(base+reg+0) << 8) | *ADDR8(base+reg+1);
  /* ncr7xx.c:976-983 write */
  *ADDR8(base+shadow+reg+0) = value >> 8;
  *ADDR8(base+shadow+reg+1) = value;
  ```
- 32-bit values can be done either as one `ADDR32` (`get_ncrreg32`, `ncr7xx.c:918`) **or** as four big-endian byte accesses (`get_ncrreg32b`/`set_ncrreg32b`, `ncr7xx.c:939-942, 970-973`). The byte-wise variants establish the **lane order is big-endian** (byte at offset+0 is the MSB).

### Byte-lane / endianness note for your detection driver
The board presents each register's bytes in **big-endian lane order**, and your kernel maps it the same way the AmigaOS code does, so **8-bit reads at the flat offsets in the table above are already correct** (your detection output proves this). For 32-bit registers (DSP, DSPS, DSA, TEMP, SCRATCH) when you get to SCRIPTS, prefer the **single 32-bit `ADDR32` access** (matching `get_ncrreg32`) so the lane mapping and the chip's native long-word handling line up — do not assemble them from 16-bit halves.

### Cache caveat (will matter once you write registers)
`a4091_validate` flushes the data cache after each shadow write before reading back (`attach.c:562-563`, `CacheClearE`). The comment (`attach.c:554-560`) explains the 68030 write-allocate bug is *avoided* precisely because writes go to the +0x40 shadow while reads come from +0x00 (different addresses). Your detection driver only reads, so this is not yet an issue — but when you start writing, write to `siop + 0x40 + reg`.

---

## 3. Chip-revision read — CONFIRMED CORRECT

Your driver's `(siop[R_CTEST8] >> 4) & 0xf` is correct.

- `siopreg.h:593`: `#define SIOP_CTEST8_V 0xf0 /* Chip revision level */`
- `ncr7xx.c:1030` (the 710 path): `ncr_runtime.revision = get_ncrreg8(REG_CTEST8) >> 4;` with `REG_CTEST8 = 0x21` (`ncr7xx.c:104`).
- `siop.c:862` (NetBSD 710 driver): prints `rp->siop_ctest8 >> 4`.

So: **CTEST8 (offset 0x21), high nibble = revision.** (For contrast, on the 720/770 it is CTEST3>>4 — `ncr7xx.c:1053`, `siopreg.h:509` — but that does not apply to your 710.)

---

## 4. Point-by-point check of your driver's offsets vs. siopreg.h (ARCH_710)

All nine offsets in `src/a4091.c:35-43` are **CORRECT**. Each verified against `siopreg.h` ARCH_710 and `ncr7xx.c` `REG_*`:

| Your define | Your value | siopreg.h ARCH_710 | ncr7xx.c | Verdict |
|---|---|---|---|---|
| `R_SIEN` | 0x00 | siop_sien @0x00 (L47) | REG_SIEN 0x00 (L79) | ✅ |
| `R_SCNTL1` | 0x02 | siop_scntl1 @0x02 (L49) | REG_SCNTL1 0x02 (L77) | ✅ |
| `R_SCNTL0` | 0x03 | siop_scntl0 @0x03 (L50) | REG_SCNTL0 0x03 (L76) | ✅ |
| `R_SCID` | 0x07 | siop_scid @0x07 (L55) | REG_SCID 0x07 (L80) | ✅ |
| `R_DSTAT` | 0x0F | siop_dstat @0x0f (L65) | REG_DSTAT 0x0f (L88) | ✅ |
| `R_CTEST8` | 0x21 | siop_ctest8 @0x21 (L82) | REG_CTEST8 0x21 (L104) | ✅ |
| `R_ISTAT` | 0x22 | siop_istat @0x22 (L83) | REG_ISTAT 0x22 (L103) | ✅ |
| `R_DCNTL` | 0x38 | siop_dcntl @0x38 (L99) | REG_DCNTL 0x38 (L115) | ✅ |
| `R_DMODE` | 0x3B | siop_dmode @0x3b (L102) | REG_DMODE 0x3b (L112) | ✅ |

**No offset is wrong.** Your register map is sound for a 710.

---

## Concrete fixes / additions for `src/a4091.c`

Nothing is broken, but these will save you pain in the next phase:

1. **None of the 9 offsets need changing.** Keep them.

2. **Add a comment that ISTAT/CTEST8 are at the high offsets because the 710 register file is 0x00-0x3f flat** — i.e. these are NOT relocated by big-endian remapping, they are genuinely at 0x21/0x22 on the chip (unlike the 720/770 where ISTAT moves to 0x17). Your current comment "A4091 big-endian byte lanes" slightly implies the offsets come from lane-swapping; they don't — the offsets are the chip's native flat layout. (Cosmetic, but it will mislead future-you.)

3. **For writes (next phase): write to `siop + 0x40 + reg`, read from `siop + reg`.** Define:
   ```c
   #define SIOP_WSHADOW 0x40    /* A4091 53C710 write shadow (ncr7xx.c:153) */
   /* read:  v = siop[reg];  write: siop[SIOP_WSHADOW + reg] = v; */
   ```
   This is the single most important thing to get right before any init/reset, and it's why your read-only detection works without it today.

4. **For the 32-bit registers (DSP@0x2c, DSPS@0x30, DSA@0x10, TEMP@0x1c, SCRATCH@0x34): use aligned 32-bit (`*(volatile uint32_t *)`) accesses, never 16-bit.** Mirror `get_ncrreg32`/`set_ncrreg32` (`ncr7xx.c:918,962`). Writes still go through +0x40 (`set_ncrreg32` writes to `base + shadow + reg`).

5. **Soft-reset sequence to add (from `siopngreset`, `siop.c`/`siop2.c:938-940`)**, all via the +0x40 shadow:
   ```
   ISTAT |= ISTAT_ABRT (0x80)   ; abort any script
   ISTAT |= ISTAT_RST  (0x40)   ; reset chip
   ISTAT &= ~ISTAT_RST          ; release
   ```
   Bit values: `SIOP_ISTAT_ABRT=0x80`, `SIOP_ISTAT_RST=0x40` (`siopreg.h:574-575`; `ncr7xx.c:178-179`).

6. **Before driving anything, burst must be disabled on the A4091**: `sc_ctest7 = SIOP_CTEST7_CDIS` (0x80) — `attach.c:630`, `siopreg.h:557`. And A4091 (Zorro III) uses `sc_dcntl = 0` (NOT `SIOP_DCNTL_EA`, which is A4000T-only) — `attach.c:638-639`.

7. **Reference files you'll actually need next** (not siop2.c): the 710 logic is in `reference/a4091-software/siop.c` (NetBSD 710 driver, `siopreset`/`siop_start`/`siopintr`), `siop_script.ss` (the 710 SCRIPTS, not siop2_script.ss), and `ncr7xx.c` (`get_ncrreg*`/`set_ncrreg*` are your portable access-primitive template). Board offsets: register file at board+`A4091_OFFSET_REGISTERS`=0x00800000 (`a4091.h:19`), confirming your `SIOP_OFF`.


# ===== INIT_SEQUENCE =====

I now have everything cross-checked. The user's own reference doc confirms most of this, but there are important discrepancies between that draft and what the **authoritative A4091 firmware `siop.c` actually does** that I should call out (DMODE value, DCNTL value, CTEST7/CTEST8 handling, SCID value, CTEST0 mask). Let me confirm the exact firmware DMODE and the CTEST8_SM A4091 quirk one more time, then deliver.

The firmware `siopreset()` (siop.c:780-900) is authoritative. Key concrete findings are confirmed. Here is the deliverable.

---

# 53C710 SOFT-RESET + INIT RECIPE for the A4091 (Amix port)

## Source of truth
The A4091 builds with `-DNCR53C710 -DARCH_710` and compiles **`siop.c`** (not `siop2.c` — that's the 720/770 `siopng_*` driver). Authoritative paths:
- `reference/a4091-software/siop.c:780` `siopreset()` — the actual register bring-up
- `reference/a4091-software/siop.c:653` `siopinitialize()` — clock/dcntl precompute, calls `siopreset()`
- `reference/a4091-software/siopreg.h:45-104` — ARCH_710 register map + bit defs
- `reference/a4091-software/attach.c:627-639` — per-board constants

## A4091 hardware constants (attach.c / a4091.h)
- Register file at `board + 0x00800000`; you already map this (`siop[]`). All 53C710 register offsets below are the **chip's native little-end offsets** from `siopreg.h:45` (e.g. SIEN=0x00, SCNTL0=0x03, ISTAT=0x22, DSP=0x2C, DCNTL=0x38). Your detection driver already uses the A4091 big-endian byte-lane offsets (CTEST8 at BE 0x21); keep using whatever single read/write accessor you validated against CTEST8.
- `sc_clock_freq = 50` (MHz, `a4091.h:69` `HW_SCLK_CLOCK_FREQ`).
- `sc_ctest7 = SIOP_CTEST7_CDIS = 0x80` (attach.c:630 — "disable burst").
- `sc_dcntl = 0` for Zorro cards (attach.c:639 — A4091 is **not** A4000T, so **no** `DCNTL_EA`).
- Host SCSI ID comes from DIP straps (`chan_id`, attach.c:668), normally 7.

## Precomputed values (siopinitialize, siop.c:688-706) for 50 MHz
`sc->sc_clock_freq = 50` falls into the `<= 50` branch (siop.c:700):
- **`sc_dcntl |= 0x00`** → SCLK/2 clock-conversion. Since Zorro `sc_dcntl` starts 0, **final DCNTL = 0x00**. (Confirms your doc's "CF=0x00 @ 50 MHz", but note the firmware does **not** OR in EA on the A4091.)
- `sc_tcp[0] = sc_tcp[3] = 2000/50 = 40`; `sc_minsync = sc_tcp[1] = 1000/50 = 20 → clamped to 25` (siop.c:691-693).

## Ordered bring-up (exactly siopreset(), siop.c:794-899)

All writes are 8-bit unless noted. "RMW" = read-modify-write of the live register.

1. **ISTAT |= ABRT (0x80)** — abort any running SCRIPT. `siop_istat |= SIOP_ISTAT_ABRT` (siop.c:800). Offset 0x22.
2. **ISTAT |= RST (0x40)** then **ISTAT &= ~RST** — software reset pulse (siop.c:801-802). This is the 53C710 **soft reset** (via ISTAT bit 6, **not** DCNTL — the 700 uses DCNTL, do not copy that). Reset is **not** self-clearing; you must clear bit 6 yourself. It restores register defaults, deasserts SCSI signals, does **not** assert SCSI RST/.
3. **SIEN = 0x00** — disable SCSI interrupts before touching the bus (siop.c:806). Offset 0x00.
4. **SCNTL1 |= RST (0x08); delay(1); SCNTL1 &= ~RST** — pulse SCSI bus reset (siop.c:807-809). Offset 0x02. *(Optional for pure INQUIRY bring-up; the firmware does it. If you skip it, skip the 250 ms settle in step 17 too — but doing the bus reset once is the clean state.)*
5. **SCNTL0 = ARB_FULL | EPC | EPG = 0xC0|0x08|0x04 = 0xCC** — full arbitration, parity check + generate (siop.c:814). Offset 0x03.
6. **SCNTL1 = ESR (0x20)** — enable selection/reselection (siop.c:815). Offset 0x02. (Note: ARCH_710 bit 0x20 is ESR, not DHP.)
7. **DCNTL = sc_dcntl = 0x00** — SCLK/2 conversion for 50 MHz, no EA on Zorro (siop.c:816). Offset 0x38. *(Your draft doc shows 0x20/EA — that is the A4000T value; for the A4091 it is 0x00.)*
8. **DMODE = 0xE0** — **A4091-specific**: burst length 8 + drive FC2. The firmware uses `0xe0` under `PORT_AMIGA` (siop.c:818), **not** the generic NetBSD `0x80` (siop.c:820). 0xE0 = BL=8 (0xC0) | FC2 (0x20). Offset 0x3B. *(Your draft doc's DMODE=0x80 is the non-Amiga value; use 0xE0.)*
9. **SIEN = 0x00** — keep SCSI ints off during the rest of setup (siop.c:822). Offset 0x00.
10. **DIEN = 0x00** — DMA ints off too (siop.c:823). Offset 0x3A.
11. **SCID = (1 << chan_id)** — host ID as a **bit mask** on the 710 (siop.c:824, `1 << sc->sc_channel.chan_id`). For ID 7 → **0x80**. Offset 0x07. *(Critical 710 quirk: unlike the 720/770 which write the raw ID | RRE | SRE, the 710 SCID is a one-hot response-ID bitmask. Your detection-doc note that mentions plain ID is wrong for the 710.)*
12. **DWT = 0x00** — DMA watchdog timer off (siop.c:825). Offset 0x39.
13. **CTEST0 |= BTD | EAN (0x40 | 0x10 = 0x50)** — byte-to-byte timer disable + enable active negation (siop.c:826, RMW). Offset 0x17. *(This is the firmware/NetBSD amiga mask; your draft flagged it LOW-confidence — it is `BTD|EAN`, confirmed at siop.c:826.)*
14. **CTEST7 |= sc_ctest7 (0x80 = CDIS)** — cache-burst disable (siop.c:827, RMW). Offset 0x18.
15. **CTEST8 |= SM (0x01)** — **A4091-specific, PORT_AMIGA only** (siop.c:835): set SC0/snoop-pins as an output to work around the 53C710 bus-arbitration errata. Offset 0x21. *(Do not skip on the A4091.)*
16. **Clear stale latched interrupts**: read **ISTAT** (0x22); if `SIP (0x02)` set, read **SSTAT0** (offset 0x0E); if `DIP (0x01)` set, read **DSTAT** (offset 0x0F). On the A4091 the firmware reads these as one 32-bit read at `&siop_sstat2` (siop.c:843-845) to clear SIP+DIP together — but byte reads of SSTAT0 and DSTAT are functionally fine. Discard the values.
17. **delay(250 ms)** — `siop_reset_delay = 250` (siop.c:857, `siop_reset_delay * 1000` µs). Lets the SCSI bus settle after the reset pulse.
18. **Read chip rev for sanity**: `CTEST8 >> 4` (siop.c:862) — you already do this; should match your detection value.

## Enable interrupts (siop.c:894-899) — do this LAST, after the delay
19. **SIEN = M_A|STO|SGE|UDC|RST|PAR = 0x80|0x20|0x08|0x04|0x02|0x01 = 0xAF** (siop.c:894). SEL is intentionally masked off. Offset 0x00.
20. **DIEN = BF|ABRT|SIR|IID = 0x20|0x10|0x04|0x01 = 0x35** (siop.c:896). WTD masked off. Offset 0x3A.

> For a first **polled** INQUIRY you can leave SIEN/DIEN = 0 and poll ISTAT for `SIP|DIP` (0x03) like `siop_poll()` does — simpler for bring-up. Enable them only once interrupt routing works.

## Launching SCRIPTS — how DSP is loaded (siop_start, siop.c:1108-1111)
For the first transaction (`nexus_list` empty), the launch sequence is exactly:
1. **TEMP = 0** (offset 0x1C, 32-bit) — `rp->siop_temp = 0` (siop.c:1108).
2. **SBCL = sc_sync[target].sbcl** (offset 0x08) — for an un-negotiated target this is 0 = async (siop.c:1109).
3. **DSA = kvtop(&acb->ds)** (offset 0x10, 32-bit) — physical address of the `siop_ds` data block (siop.c:1110). On Amix this is the physical (bus) address of your DS struct.
4. **DSP = sc_scriptspa** (offset 0x2C, 32-bit) — physical address of the assembled SCRIPTS image (siop.c:1111). **Writing DSP is what starts the SCRIPTS processor.** `sc_scriptspa` is set in `siopinitialize` via `get_scripts_dma_addr(scripts, sizeof(scripts))` (siop.c:668) — the SCRIPTS come from `siop_script.out`.

All of DSA/DSP/TEMP are **32-bit** registers — use your 32-bit accessor, and these addresses must be **physical/bus addresses** the chip can DMA (kvtop equivalent under Amix), not kernel VAs. The SCRIPT itself reads the DS at DSA for the ID/CMD/STATUS/MSG buffer pointers, so every pointer you put in `siop_ds` must likewise be physical.

## Ordering constraints / A4091 quirks to not get wrong
- **Soft reset is ISTAT bit 6 (0x40), self-clear required** — not DCNTL. (siop.c:801-802)
- **SCID is a one-hot bitmask** `1<<id` on the 710 (=0x80 for ID 7), and has **no** RRE/SRE bits (those are 720/770 only, siopreg.h:313). (siop.c:824)
- **DMODE = 0xE0** on the A4091 (FC2 + burst-8), not 0x80. (siop.c:818)
- **DCNTL = 0x00** on the A4091 (Zorro, no EA), CF=SCLK/2 for 50 MHz. (siop.c:639,816,700-701)
- **CTEST8 |= SM (0x01)** errata work-around is mandatory on the A4091. (siop.c:835)
- **Interrupts must be 0 during setup** (steps 9-10) and only enabled after the 250 ms post-reset delay (steps 19-20). (siop.c:822-823 vs 894-899)
- **CTEST7 (CDIS) and CTEST8 (SM) are RMW** (`|=`), so read the current value through your accessor first.
- Because your detection driver already proved correct byte-lane reads via CTEST8, the **write path** is the remaining unknown: per your project notes, A4091 register writes land in the shadow at `reg+0x40` and must be 8/32-bit only — make sure your write accessor targets the shadow and never does a 16-bit access. The only 16-bit-looking registers here (SIEN on a 720/770) do **not** exist on the 710: on ARCH_710 SIEN/SCID/etc. are all 8-bit (siopreg.h:47-102), and DSA/DSP/DNAD/TEMP/SCRATCH are 32-bit — so you only ever need 8-bit and 32-bit accesses, which matches the A4091 constraint exactly.

## Minimal C skeleton against your sptalloc window
```
/* native 53C710 offsets (siopreg.h ARCH_710); wrap WR8/WR32 to hit the
   A4091 write-shadow at +0x40 and use only 8/32-bit accesses. */
#define SIEN 0x00  SCNTL1 0x02  SCNTL0 0x03  SBCL 0x08  SSTAT0 0x0E
#define DSTAT 0x0F  DSA 0x10(32)  CTEST7 0x18  TEMP 0x1C(32)
#define CTEST8 0x21  ISTAT 0x22  DSP 0x2C(32)  SCID 0x07
#define DWT 0x39  DIEN 0x3A  DMODE 0x3B  DCNTL 0x38  CTEST0 0x17

WR8(ISTAT,  RD8(ISTAT) | 0x80);          /* 1  abort script        */
WR8(ISTAT,  RD8(ISTAT) | 0x40);          /* 2  soft reset...        */
WR8(ISTAT,  RD8(ISTAT) & ~0x40);         /*    ...clear (not self)  */
WR8(SIEN,   0x00);                       /* 3                       */
WR8(SCNTL1, RD8(SCNTL1) | 0x08); udelay(1); WR8(SCNTL1, RD8(SCNTL1) & ~0x08); /* 4 bus reset pulse */
WR8(SCNTL0, 0xCC);                       /* 5  ARB_FULL|EPC|EPG     */
WR8(SCNTL1, 0x20);                       /* 6  ESR                  */
WR8(DCNTL,  0x00);                       /* 7  CF=SCLK/2 @50MHz     */
WR8(DMODE,  0xE0);                       /* 8  burst8|FC2 (A4091)   */
WR8(SIEN,   0x00);                       /* 9                       */
WR8(DIEN,   0x00);                       /* 10                      */
WR8(SCID,   1 << host_id);               /* 11 one-hot (0x80 for 7) */
WR8(DWT,    0x00);                        /* 12                      */
WR8(CTEST0, RD8(CTEST0) | 0x50);         /* 13 BTD|EAN              */
WR8(CTEST7, RD8(CTEST7) | 0x80);         /* 14 CDIS                 */
WR8(CTEST8, RD8(CTEST8) | 0x01);         /* 15 SM errata (A4091)    */
{ u8 i = RD8(ISTAT);                      /* 16 clear stale ints     */
  if (i & 0x02) (void)RD8(SSTAT0);
  if (i & 0x01) (void)RD8(DSTAT); }
udelay(250000);                          /* 17 settle               */
/* polled bring-up: leave SIEN/DIEN at 0 and poll ISTAT&0x03.       */
/* or enable: WR8(SIEN,0xAF); WR8(DIEN,0x35);                       */ /* 19,20 */

/* launch SELECT+INQUIRY SCRIPT: */
WR32(TEMP, 0);                           /* siop.c:1108             */
WR8 (SBCL, 0);                           /* async                   */
WR32(DSA,  phys_of(ds));                  /* siop.c:1110             */
WR32(DSP,  scripts_pa);                   /* siop.c:1111 -> STARTS   */
```

The `siop_ds` layout you must populate before launch is in `siopvar.h:56-76` (scsi_addr, then len/buf pairs for Identify, Command, Status, Message; `siop_start` fills `acb->ds.scsi_addr = (0x10000<<target) | (sxfer<<8)` at siop.c:950, all buffer pointers via `kvtop`). The Identify byte is `MSG_IDENTIFY|lun = 0x80|lun` (siop.c:943). All DS buffer pointers and DSA/DSP must be physical addresses.


# ===== SCSI_SCRIPTS =====

All encodings confirmed against the real assembler. I have a complete, verified picture. Here is the deliverable.

---

# Issuing one INQUIRY via the 53C710 SCRIPTS engine — recipe for the A4091 Amix driver

## 0. CRITICAL CORRECTION before anything else

**The A4091 is a real 53C710, not a 53C770.** The task pointed at `siop2.c` / `siop2_script.ss` — those are the **ARCH_770** files for the A4000T/A4770. The A4091 build in this tree uses **`-DARCH_710`** (`Makefile:20`: `-DDRIVER_A4091 -DNCR53C710=1 -DARCH_710`). The driver that actually matches your chip is **`siop.c` / `siop_script.ss`**, and the register map is the `ARCH_710` branch of `siopreg.h` (lines 45-104). The two diverge in ways that matter:

- 710 has **no SCNTL2/SCNTL3, no GPREG, no SIST** (the 770 SCRIPTS poke `GPREG` and `SCNTL2` — those registers don't exist on the 710; your reference doc §"Registers that DO NOT EXIST" line 139 says the same).
- 710 register file is exactly `0x00`–`0x3F`; reselect-ID is saved via **`LCRC`/`SCRATCH0`** (710) not `SSID`/`SCRATCHJ0` (770).
- The 710 `MOVE FROM` is **table-indirect through DSA** and is the canonical A4091 path.

Everything below is derived from the **710** files and from the **actual assembler output** I generated (`/tmp/ncr53cxxx` built from `reference/a4091-software/ncr53cxxx.c`, run on `siop_script.ss` → `/tmp/siop_script.out`).

---

## 1. Plain-English walk-through, mapped to `siop_script.ss`

Reference: `reference/a4091-software/siop_script.ss`. Assembled offsets from `/tmp/siop_script.out` (entry table at its bottom).

| Phase | SCRIPTS source (`siop_script.ss`) | What the chip does |
|---|---|---|
| **Arbitration + SELECT (with ATN)** | `scripts:` line 74 `SELECT ATN FROM ds_Device, REL(reselect)` | Arbitrates, selects target whose one-hot ID is in the DSA `ds_Device` longword, **asserts ATN** (so the target goes to MSG_OUT first). word2 is the jump taken **only if the chip is reselected instead of winning selection**. A select **timeout** does not jump here — it raises `SSTAT0.STO` and interrupts the host (handled in C, `siop.c:1575`-region for 770 / equivalent STO path for 710). |
| **dispatch** | `switch:` line 76-84: `JUMP REL(msgin) WHEN MSG_IN` … `JUMP REL(end) IF STATUS`, else `INT err5` | Reads the real bus phase (REQ asserted) and branches. This is the re-entry point after every phase (`Ent_switch = 0x08`). |
| **MESSAGE OUT (IDENTIFY)** | `msgout:` line 163-165 `MOVE FROM ds_MsgOut, WHEN MSG_OUT` then `JUMP switch` | DMAs the IDENTIFY byte(s) from the buffer pointed to by the `ds_MsgOut` table entry. ATN was set by SELECT; after this the target normally drops to COMMAND. |
| **COMMAND (CDB)** | `command_phase:` line 167-170 `CLEAR ATN` / `MOVE FROM ds_Cmd, WHEN CMD` / `JUMP switch` | Clears ATN (we have nothing more to say), then DMAs the 6-byte CDB out. |
| **DATA IN** | `datain:` line 192-210: nine `MOVE FROM ds_DataN, WHEN DATA_IN` each followed by `CALL switch WHEN NOT DATA_IN` | DMAs INQUIRY data into host RAM. The 9 chained moves are scatter/gather slots; **for one contiguous 36-byte buffer only `ds_Data1` is used** and the `CALL switch WHEN NOT DATA_IN` fires as soon as the target leaves DATA_IN, returning to dispatch. (`Ent_datain = 0x270`.) |
| **STATUS** | `end:` line 212-213 `MOVE FROM ds_Status, WHEN STATUS` | DMAs the 1 status byte into `stat[0]`. |
| **MESSAGE IN (command complete)** | `end:` line 214-218 `MOVE FROM ds_Msg, WHEN MSG_IN` / `CLEAR ACK` / `WAIT DISCONNECT` / `INT ok` | DMAs the COMMAND COMPLETE message (0x00) into `msg[0]`, releases ACK, waits for the target to drop BSY, then **`INT ok` (DSPS = 0xff00)** — the chip halts and interrupts the host. That `0xff00` is what `siop_checkintr` keys on as "done" (`siop.c:1342` for 710 path / `siop2.c:1349`). |

The `msgin:` block at line 86-95 is the general message decoder (handles disconnect, SDTR, reject, etc.); for a clean INQUIRY to a quiet target you never enter it except for the final COMMAND COMPLETE handled inline in `end:`.

---

## 2. Data structures the SCRIPTS read — the DSA layout (concrete offsets)

The DSA (register `0x10`, `siop_dsa`) points at `struct siop_ds` (`siopvar.h:56-76`). With **table-indirect** moves, each `MOVE FROM ds_X` reads a **{32-bit length, 32-bit physical buffer pointer}** pair at a fixed DSA offset. The `ABSOLUTE ds_*` symbols (siop_script.ss:32-48) and the assembled `#define A_ds_*` (`/tmp/siop_script.out:112-128`) give the offsets exactly:

```
DSA offset  field            len/ptr pair             set in siop.c (siop_start)
 0x00       ds_Device        scsi_addr (one longword, NOT a len/ptr pair — read by SELECT)
 0x04       ds_MsgOut        idlen / idbuf            -> &acb->msgout[0]   (IDENTIFY)         siop.c:951-952
 0x0c       ds_Cmd           cmdlen / cmdbuf          -> the CDB                              siop.c:953-954
 0x14       ds_Status        stslen / stsbuf          -> &acb->stat[0]     (1 byte)           siop.c:955-956
 0x1c       ds_Msg           msglen / msgbuf          -> &acb->msg[0]      (cmd-complete msg) siop.c:957-958
 0x24       ds_MsgIn         msginlen / msginbuf      -> &acb->msg[1]                         siop.c:960,963
 0x2c       ds_ExtMsg        extmsglen / extmsgbuf    -> &acb->msg[2]                         siop.c:961,964
 0x34       ds_SyncMsg       synmsglen / synmsgbuf    -> &acb->msg[3]                         siop.c:962,965
 0x3c       ds_Data1         chain[0].datalen / databuf  (the 36-byte INQUIRY buffer)        siop.c:1049 ...
 0x44       ds_Data2 ... 0x7c ds_Data9   (scatter/gather slots, unused for one buffer)
```

`ds_Device` (`scsi_addr`) is special: `siop.c:950` sets it to `(0x10000 << target) | (sxfer<<8)`. The high 16 bits are the **one-hot SELECT ID** (`1<<(16+target)`), the middle byte is the sync-transfer (`SXFER`) value loaded by SELECT. **All length/pointer values are natural big-endian longwords; SCSI payload bytes are never swapped** (your ref doc §6.3).

Each MOVE's word1 carries the **DSA offset in its low 24 bits** and the phase+table-indirect bits in the top byte. Confirmed encodings (high byte = `0x10` table-indirect | `0x08` "when phase" | `phase<<24`):

```
MOVE FROM ds_MsgOut, WHEN MSG_OUT  = 0x1e000004   (phase 6, offset 0x04)
MOVE FROM ds_Cmd,    WHEN CMD      = 0x1a00000c   (phase 2, offset 0x0c)
MOVE FROM ds_Data1,  WHEN DATA_IN  = 0x1900003c   (phase 1, offset 0x3c)   [datain entry]
MOVE FROM ds_Status, WHEN STATUS   = 0x1b000014   (phase 3, offset 0x14)
MOVE FROM ds_Msg,    WHEN MSG_IN   = 0x1f00001c   (phase 7, offset 0x1c)
```
(word2 of a table-indirect move equals the same DSA offset — see `/tmp/siop_script.out` lines 62-105.)

---

## 3. The absolute minimum for ONE INQUIRY — yes, a tiny custom SCRIPTS fragment works

You do **not** need `siop.c`'s full ACB/queue/sync-negotiation machinery. One INQUIRY needs: a soft reset + minimal init, a small data structure, a ~15-instruction SCRIPTS program, one DSP write, and a poll on ISTAT. I verified the exact program assembles cleanly (see `/tmp/inq.ss` → `/tmp/inq.out`, 15 instructions / 120 bytes).

### 3a. Two ways to encode the moves
- **Table-indirect (matches the real driver, recommended):** moves read len/ptr from the DSA. You set `DSA` and one small `siop_ds`-like table. This is what `/tmp/inq.out` shows.
- **Direct addressing (your ref doc §5.5):** each move carries `[count|phys]` inline; no DSA needed. Simpler to hand-assemble but diverges from the driver. Either works on the 710.

I give the **table-indirect** version since it is byte-for-byte the path you'll grow into, and it's already proven against the assembler.

### 3b. Minimal init (710-correct subset of `siopreset`, `siop.c:780`-900)
All at `R = 0x40800000`, **8-bit writes**, and remember the **A4091 write-shadow at reg+0x40** (ref doc line 214): writes to a register go to `reg+0x40`, reads from `reg+0x00`.
```
ISTAT(0x22) |= 0x80   ; ABRT — abort any running script
ISTAT(0x22) |= 0x40   ; RST=1 soft reset chip
ISTAT(0x22) &= ~0x40  ; RST=0
SCNTL0(0x03) = 0xC4   ; ARB_FULL(0xC0)|EPG(0x04)   (EPC optional)
SCNTL1(0x02) = 0x20   ; ESR enable sel/resel (710 bit)
DCNTL(0x38)  = 0x00   ; CF = SCLK/2 for ~50MHz (ref §line 161). MAN/SSM stay 0 so DSP-write auto-starts
DMODE(0x3B)  = 0xE0   ; burst 8, drive FC2 (A4091, siop.c:818). 0x00 also works to start
SCID(0x07)   = 1<<7   ; host is ID 7 -> 0x80
SIEN(0x00)   = 0x00   ; poll, no SCSI ints
DIEN(0x3a)   = 0x00   ; poll, no DMA ints
CTEST8(0x21) high nibble readback = chip rev (you already read this)
```
Optionally also assert SCSI bus reset once (`SCNTL1 |= 0x08; delay; &= ~0x08`) to quiesce the bus, then `delay(250ms)`. Skip the whole sync/wide negotiation path — leave `SXFER=0` (async).

### 3c. The data structure (one cache-inhibited or flushed page)
Put all of this in physically-contiguous, cache-coherent memory (CI page is simplest on the 68030 — ref §6.4). Let `P(x)` = physical address.
```
IDENT  : 1 byte   = 0x80 | LUN        ; IDENTIFY, disconnect FORBIDDEN (no reselect to handle)
CDB    : 6 bytes  = 12 00 00 00 24 00 ; INQUIRY, alloc len 0x24=36
DATAIN : 36 bytes (zeroed)
STATUS : 1 byte
MSGIN  : 1 byte

DSA table (struct, longwords BIG-ENDIAN, no swap):
  +0x00 scsi_addr = (1 << (16+target))        ; target0 -> 0x00010000 ; SXFER=0 (async)
  +0x04 idlen=1            ; +0x08 idbuf = P(IDENT)
  +0x0c cmdlen=6           ; +0x10 cmdbuf= P(CDB)
  +0x14 stslen=1           ; +0x18 stsbuf= P(STATUS)
  +0x1c msglen=1           ; +0x20 msgbuf= P(MSGIN)        ; final cmd-complete msg lands here
  +0x24 msginlen=1         ; +0x28 msginbuf= P(MSGIN)      ; (unused in clean path; keep non-null)
  +0x2c extmsglen=1        ; +0x30 extmsgbuf=P(MSGIN)      ; (unused; keep non-null)
  +0x34 synmsglen=1        ; +0x38 synmsgbuf=P(MSGIN)      ; (unused; keep non-null)
  +0x3c data1len=36(0x24)  ; +0x40 data1buf= P(DATAIN)
```
(Mirror of `siopvar.h` `siop_ds`; offsets exactly match `A_ds_*`. Lengths/ptrs are what the table-indirect moves fetch.)

### 3d. The minimal SCRIPTS program (table-indirect) — assembled bytes verified
This is `/tmp/inq.out`, relocated. Store as an array of **big-endian u_int32**; place it in coherent memory; let `SP = P(scripts[0])`. word2 of each `REL` jump is a **byte displacement** the chip adds to (DSP after fetch); easiest is to keep my source and re-assemble, OR patch them as absolute. Bytes (each line = 8-byte instruction, word1 then word2):

```
; ---- offset  word1        word2 ------------------------------------------------
0x00   0x47000000   <relSEL>     ; SELECT ATN FROM ds_Device, REL(seltimeout)
0x08   0x868b0000   <rel+0>      ; JUMP REL(msgout), WHEN MSG_OUT
0x10   0x1e000004   0x00000004   ; MOVE FROM ds_MsgOut, WHEN MSG_OUT   (IDENTIFY out)
0x18   0x828b0000   <rel+0>      ; JUMP REL(cmd), WHEN CMD
0x20   0x60000008   0x00000000   ; CLEAR ATN
0x28   0x1a00000c   0x0000000c   ; MOVE FROM ds_Cmd, WHEN CMD          (CDB out)
0x30   0x818b0000   <rel+0>      ; JUMP REL(data), WHEN DATA_IN
0x38   0x1900003c   0x0000003c   ; MOVE FROM ds_Data1, WHEN DATA_IN    (36 bytes in)  [offset 0x3c!]
0x40   0x838b0000   <rel+0>      ; JUMP REL(status), WHEN STATUS
0x48   0x1b000014   0x00000014   ; MOVE FROM ds_Status, WHEN STATUS    (1 byte)
0x50   0x1f00001c   0x0000001c   ; MOVE FROM ds_Msg, WHEN MSG_IN       (cmd-complete msg)
0x58   0x60000040   0x00000000   ; CLEAR ACK
0x60   0x48000000   0x00000000   ; WAIT DISCONNECT
0x68   0x98080000   0x0000ff00   ; INT ok    (DSPS=0xff00 -> "done")
0x70   0x98080000   0x0000ff10   ; seltimeout: INT 0xff10
```
Notes:
- My `/tmp/inq.ss` used `ds_DataIn=0x24`; **change it to `ds_Data1=0x3c`** to match the real `siop_ds` layout above (that's the only edit vs. the assembled listing — reflected in the table here).
- The `<rel...>` word2 values are PC-relative byte displacements the assembler computed (e.g. SELECT's was `0x00000068` in my build = jump to `seltimeout`). **Simplest path: keep `/tmp/inq.ss`, fix the `ds_DataIn`→`ds_Data1=0x3c` line, re-run `/tmp/ncr53cxxx inq.ss -p inq.out`, and paste the resulting `scripts[]` array verbatim** — the relative offsets self-resolve and you avoid hand-computing them.
- The JUMP-WHEN guards before each MOVE are belt-and-suspenders; the bare 8-instruction path (SELECT, 5 MOVEs, WAIT DISC, INT) from your ref doc §5.5 also works on a well-behaved target. Keep the guards — they turn an unexpected phase into a clean `INT err5` instead of a hang.
- **Never emit a zero-length MOVE** (DBC=0 → `DSTAT.IID`, ref §6.2). All five lengths above are non-zero.

### 3e. Launch + poll (no interrupts needed)
```
flush/with CI page: ensure SCRIPTS, table, IDENT, CDB are visible to the chip
writel(R+0x10, P(DSA table))      ; DSA
writel(R+0x2C, P(scripts[0]))     ; DSP  <-- top byte (0x2F) write triggers SCRIPTS auto-start
poll ISTAT(R+0x22):
    SIP(0x02) -> read SSTAT0(0x0E): STO(0x20)=select timeout (no target),
                                    MA(0x80)=phase mismatch, UDC(0x04)=disconnect
    DIP(0x01) -> read DSTAT(0x0F):  SIR(0x04)=SCRIPTS INT (read DSPS@0x30):
                                        0xff00 = DONE  (success)
                                        0xff10 = our select-timeout vector
                                        0xff05 = unexpected phase (err5)
                                    BF(0x20)=bus fault (bad phys addr), IID(0x01)=illegal instr
on 0xff00: invalidate DATAIN cache, read 36 bytes; STATUS byte should be 0x00 (GOOD),
           MSGIN byte should be 0x00 (COMMAND COMPLETE).
```
INQUIRY data byte 0 low 5 bits = peripheral device type (0x00 = direct-access disk); bytes 8-15 = vendor, 16-31 = product — your proof the path works end to end.

---

## Concrete file references
- Correct driver/SCRIPTS for the A4091 (710): `reference/a4091-software/siop.c`, `reference/a4091-software/siop_script.ss`
- DSA setup in C: `siop.c:943-965` (`siop_start`), launch at `siop.c:1108-1111`, "done" detection at `siop.c:1342`
- Minimal init: `siop.c:800-826` (the 710 `siopreset` body)
- Register offsets (710): `siopreg.h:45-104` (ARCH_710 branch)
- DSA struct: `siopvar.h:56-76`; ACB buffers `msgout/msg/stat`: `siopvar.h:98-101`
- Assembler: `reference/a4091-software/ncr53cxxx.c` (block-move table-indirect encoder at lines 1300-1331; SELECT at 1231-1262; INT/transfer at 1151-1229) — **builds cleanly with `gcc -O2 -o ncr53cxxx ncr53cxxx.c`**
- Verified assembled output I generated: `/tmp/siop_script.out` (full driver script + `Ent_*`/`A_ds_*` tables) and `/tmp/inq.out` (the minimal 15-instruction INQUIRY program)
- Your own reference doc (already aligned): `docs/a4091-53c710-reference.md` §5 (SCRIPTS model + minimal INQUIRY) and §6 (DMA/cache); note its §5.5 sketch uses **direct** addressing while the recipe above uses **table-indirect** to match the driver.

**Wrong files for the A4091 (these are 770/A4000T):** `siop2.c`, `siop2_script.ss`, and the `ARCH_720/770` branch of `siopreg.h` — do not use their `GPREG`/`SCNTL2`/`SSID` register pokes on the 710.


# ===== ROM_BOOTCRASH =====

I have a complete analysis. Here is the deliverable.

---

# A4091 cold-boot crash analysis: Guru `0xD2454C41`, Task `0x07803248`

## 1. Exact early-boot sequence (AutoConfig → autoboot)

The A4091 ROM image is `rom.S` + (appended) ZX0-compressed `a4091.device`. During Kickstart's expansion phase:

1. **AutoConfig (`rom.S:73-108`, `RomStart`)** — The Zorro III config logic reads the nibble-packed `ExpansionRom` struct (two 128-byte half-arrays). MANUF=514, PROD=84, type = ZORRO_III|AUTOBOOT_ROM (`rom.S:27-36`). The board is placed in the ConfigDev table at its 16MB-aligned base. This is the ONLY part you actually need for Amix.
2. **Diag copy** — Because `da_Config = DAC_NIBBLEWIDE+DAC_CONFIGTIME` (`rom.S:116`), expansion.library copies the `DiagStart..EndCopy` area into RAM and calls `DiagEntry`.
3. **`DiagEntry` (`rom.S:152`)** — Patches the in-RAM Romtag (MatchTag/EndSkip/Name/IdString self-pointers). Checks left/right mouse (`rom.S:171-191`): **left mouse held + right not held = skip driver load entirely** (`.no_resident`, zeroes the romtag at `rom.S:225-227`). Otherwise probes ROM for the `/CDH` magic `$2f434448` at 64K-4 / 32K-4 (`rom.S:197-220`) to find the device length.
4. **Relocate + decompress (`rom.S:230-236` → `reloc.S` `_relocate`)** — `_relocate` reads the hunk header; if it's `$5a583001` ("ZX0\1") it `AllocMem`s the uncompressed buffer, copies the compressed image from the nibble-mapped ROM, runs `zx0_decompress` (`reloc.S:51-110`, `unzx0_68000.S`), then `.real_relocate` walks HUNK_CODE/DATA/BSS/RELOC32 building a SegList in fresh `MEMF_PUBLIC` RAM. **The decompressed driver lands wherever AllocMem returns — on a stock A4000 with the fast-RAM allocator that's the ~`0x0780xxxx` region you observed.** Returns SegList in d0; Romtag `RT_INIT` is patched to the device's `_auto_init_tables` (`rom.S:256-268`).
5. **Resident init** — After AutoConfig, exec's `InitCode(RTF_COLDSTART)` (via `Resident`, pri 10, `rom.S:312-315`) calls the device's `init()` (`device.c:451`). `seg_list == 0` here, so **`romboot = TRUE`** (`device.c:459-460`).
6. **`init()` → `init_device()` → `start_cmd_handler()`** (`device.c:462,181`; `cmdhandler.c:896`) — `CreateTask(real_device_name, ... cmd_handler, 8192)` spawns the driver task. **This task's code/stack live in the `~0x0780xxxx` AllocMem'd region — `Task: 0x07803248` is exactly this `a4091.device` cmd-handler task.** That task runs `init_chan()` → `a4091_find()` (`GetCurrentBinding`, ROM path, `attach.c:307-328`) → `a4091_validate()` → `siopinitialize()` which **issues a 53C710 chip+SCSI bus reset** (`siop2.c:939-940`, `SIOP_ISTAT_RST`).
7. **Autoboot scan (`device.c:468-472`, romboot only)** — `init_romfiles()`, then **`mount_drives()`** (`device.c:407`) → `MountDrive()` (`mounter.c:1492`). This loops targets 0-7 (`mounter.c:1518`), `OpenDevice()` each (which triggers `attach()`→`scsi_probe_device()`→**SELECT + INQUIRY via SCRIPTS**, `scsiconf.c:875`, `attach.c:840`), then `ScanRDSK()` reads up to `RDB_LOCATION_LIMIT` blocks (`mounter.c:1031-1045`) and `ParseRDSK`/`ParsePART`/`ParseFSHD` to mount partitions and honor `RDBFF_LAST`/`RDBFF_LASTLUN` (`mounter.c:1024-1025`, `1579`). Finally `boot_menu()` (`device.c:471`) — runs only if right mouse is held.

So at `~0x07803248` you are **inside the freshly-decompressed `a4091.device` cmd-handler task, during step 6/7: chip reset + SELECT/INQUIRY + RDB parse of attached SCSI targets**, before the Amix kernel banner.

## 2. Decoding the alert `0xD2454C41` / Task `0x07803248`

- **Task `0x07803248`** = the `a4091.device` driver task, whose code/stack are in the RAM block the ROM relocator AllocMem'd (your `~0x07800000`). Confirms the fault is *in the relocated A4091 driver*, not in Kickstart or Amix.
- **`0xD2454C41`**: low word `0x4C41` = ASCII `"LA"`; the full longword `D2 45 4C 41` is `0x52454C41` (`"RELA"`) with bit 31 set. **This is not a defined exec/Intuition alert constant** (the A4091 source contains **zero** `Alert()` calls — grep confirmed). bits 31 **and** 30 both set is not a legal exec subsystem-id encoding. That pattern — an ASCII-looking longword shown verbatim as the "Error:" code in a *recoverable Software Failure* requester — is the classic signature of a **68k processor exception (trap) whose faulting instruction/PC pointed into ASCII string data or garbage**, i.e. the CPU jumped through a corrupted/uninitialized pointer (or executed data) and Intuition's trap handler displayed the bad longword. In short: **a wild branch / bad function pointer inside the relocated driver**, consistent with a relocation/decompression or a NULL/foreign-data deref during the SCSI/RDB scan.

## 3. Ranked likely causes

**#1 — 53C710 emulation mismatch in Amiberry causing the driver's SCSI/SCRIPTS init or DMA to run off the rails (most likely).**
The driver does a real chip reset (`siop2.c:939`) and then DMA-driven SCRIPTS SELECT/INQUIRY against Amiberry's emulated 53C710. The A4091's quirks are unusual: register **writes go to the +0x40 shadow** (`attach.c:532`), **8/32-bit-only access**, big-endian lane mapping. If Amiberry's A4091/53C710 model doesn't implement the write-shadow or SCRIPTS exactly, `siopinitialize`/the first SELECT can fetch a bad SCRIPTS pointer or complete DMA into the wrong address, then the driver branches through garbage → the `"RELA"`-looking trap. This fits "before the Amix banner, in the 0x0780 driver task." (Note `a4091_validate` at `attach.c:538-588` will `panic()` on a scratch/temp mismatch — see port.h:55 — but panic would more likely hang/serial-print; the Guru points to a wild jump after init.)

**#2 — A bad/foreign RDB on an attached disk crashing the parser during the autoboot scan.**
`mount_drives()`→`MountDrive()` parses RDB/PART/FSHD on every target (`mounter.c`). If a connected image has a foreign RDB (real Amix/UNIX disks often do NOT have an Amiga RDB, or have one the parser mis-reads), `ParseFSHD`/`fsrelocate` will try to **load and relocate a "filesystem" from LSEG blocks** (`mounter.c:398,732,980`) and jump into it / build a SegList from junk. `fsrelocate` does `AllocMem(totalHunks*...)` and pointer math driven entirely by on-disk values; a malformed FSHD yields a corrupt SegList → wild branch. This is a strong #2 because it directly explains an ASCII-garbage PC.

**#3 — `RDBFF_LAST` / `RDBFF_LASTLUN` mis-set on the first scanned disk (the README issue).**
If target 0 (or first found) has `RDBFF_LAST` set, `MountDrive` stops scanning (`mounter.c:1579`). That alone doesn't crash, but combined with `ms->luns`/`wasLastLun` logic (`mounter.c:1574`) and a misconfigured disk it can leave the boot in an inconsistent partial-mount state. More importantly the README explicitly calls this out as the "one bad/misconfigured drive blocks the rest" failure mode — relevant if your Amix disk's RDB is half-written.

**#4 — Relocation/decompression corruption (ZX0 or HUNK_RELOC) producing bad code.**
`reloc.S` is hand-written; if the ROM image, the ZX0 stream, or the RELOC32 tables are slightly off (e.g. wrong ROM size detection at `rom.S:197/211`, or a 32K-vs-64K mismatch), the relocated driver has wrong pointers and the very first indirect call traps with an ASCII-ish PC. Lower ranked because detection already works for you, implying the image relocates — but a marginal ROM build can still misrelocate the *device* portion specifically.

**#5 — DIP/ID/termination assumptions: host ID collision or `chan_id` self-select.**
`get_host_id()` reads `HW_OFFSET_SWITCHES` (`attach.c:393-399`); if Amiberry returns `0xff`/0 for the DIP register, host ID and target/LUN counts are wrong. `attach()` guards `target==chan->chan_id` (`attach.c:819`), but a wrong host ID means the controller tries to SELECT itself or scans 8 LUNs into nonexistent targets, multiplying the exposure to #1/#2.

## 4. Concrete mitigations (ranked — to get a clean cold boot)

You do **not** need the A4091's AmigaOS autoboot at all; you only need it to AutoConfig so the Amix kernel's ConfigDev table sees it. So the goal is: **let the board AutoConfig but prevent the autoboot driver from loading/scanning.**

**A. Hold the LEFT mouse button during power-up (zero rebuild, do this first).**
`rom.S:171-191`: left-mouse-down + right-mouse-up at DiagEntry → `.no_resident` → the romtag is zeroed and **`a4091.device` is never made resident, never relocated, never scans SCSI**. AutoConfig has already happened (step 1-2), so the board still appears in ConfigDev for Amix. This is the single cleanest test: if the Guru disappears, the crash is 100% in the relocated driver's init/scan (confirming #1/#2), and Amix can boot. In Amiberry, map a host key to "left mouse" and hold it through the early boot.

**B. Use the `a4091_nodriver.rom` image instead of the full ROM.**
The build produces `a4091_nodriver.rom` — "without the driver, useful for diagnostics or loading the driver from disk" (`README.md:51`). It AutoConfigs identically but has no embedded `a4091.device`, so `DiagEntry`'s `/CDH` probe (`rom.S:199-220`) fails → `.no_resident` → no relocate, no scan, no Guru. **This is the best permanent fix for your use case**: the board configures, Amix's own kernel driver does all the real SCSI work. Point Amiberry's A4091 ROM at `a4091_nodriver.rom`.

**C. Boot menu — hold RIGHT mouse, set "Ignore RDBFF_LAST" and disable "CDROM Boot".**
If you do want the AmigaOS driver but it's a bad-RDB/`RDBFF_LAST` problem (#2/#3): hold right mouse during boot to enter `boot_menu()` (`device.c:471`, `bootmenu.c:1765`). On the Debug page (`bootmenu.c:1029`) toggle **"Ignore RDBFF_LAST"** (`asave->ignore_last`, gadget id 11) so one disk can't abort the scan, and uncheck **"CDROM Boot"** (id 10) to skip the CD TOC/PVD path (`mounter.c:1542-1550`, `ScanCDROM`). These persist via battmem (`battmem.c`). Note: reaching this menu requires the driver to have *initialized* (chip reset + task running) — so it only helps if the crash is in the *scan/parse* (#2/#3), not in chip init (#1).

**D. Remove/blank the RDB on the attached Amix disk image (kills #2/#3 at the source).**
The autoboot parser only crashes on disks that *have* an RDB it tries to parse. If your Amix/UNIX disk image has a foreign or half-written RDB, either (a) zero the first `RDB_LOCATION_LIMIT` (16) blocks so `ScanRDSK` finds nothing (`mounter.c:1034`) and `MountDrive` cleanly skips it, or (b) detach all SCSI targets in Amiberry during the AmigaOS phase. Combined with A or B this guarantees no parser exposure. The bundled `RDBFlags` tool (`3rdparty/RDBFlags/`) specifically toggles the LastDisk flag if you want to keep the RDB but clear `RDBFF_LAST`.

**E. Verify/replace the ROM image and check Amiberry's 53C710 fidelity (addresses #1/#4).**
If A/B prove the board AutoConfigs fine without the driver but you eventually want the driver working under emulation: confirm Amiberry models the A4091 register **write-shadow at +0x40** and SCRIPTS DMA. If it doesn't, the embedded driver will always trap at SELECT regardless of menu settings — so `a4091_nodriver.rom` (B) is the correct path for an Amix host that has its own kernel driver. Also re-flash/rebuild from a known-good `a4091.rom` to rule out a marginal ZX0/RELOC image (#4); confirm the ROM is the size DiagEntry expects (32K vs 64K, `rom.S:197/211`).

### Bottom line
The Guru is a wild branch **inside the freshly-relocated `a4091.device` task** (Task `0x07803248` in the ROM-relocated `~0x0780xxxx` block; `0xD2454C41` = `"RELA"`+bit31 = a trap with an ASCII-garbage PC), happening during the AmigaOS autoboot driver's **chip init + SELECT/INQUIRY + RDB scan** — not in Amix. Since you only need AutoConfig, the fastest clean boot is **(A) hold LEFT mouse at power-up** to skip driver residency, and the proper permanent fix is **(B) flash `a4091_nodriver.rom`** so the board configures into the ConfigDev table while the embedded driver never loads, scans, or traps — leaving all real SCSI to your Amix kernel driver.

Key file refs: `rom.S:171-191` (mouse skip), `rom.S:199-227` (/CDH probe → no_resident), `device.c:459-472` (romboot autoboot path), `attach.c:307-328,532,938-950` (ROM find + write-shadow), `mounter.c:1024-1025,1031-1045,1492-1599` (RDB scan + RDBFF_LAST), `bootmenu.c:1029-1092` (ignore-last / cdrom-boot toggles), `siop2.c:939-940` (chip/bus reset).


# ===== BUILD_TOOLS =====

I have everything. Here is the deliverable.

---

# A4091 reference software — build / config / debug / releases / romtool

Scope note up front (since your target is **Amix, not AmigaOS**): this whole reference tree is the *AmigaOS* driver + AutoConfig ROM. The **boot menu, the RDB "mounter", the battmem CD-boot/ignore-last settings, and the `a4091.device` autoboot scan are all AmigaOS-side** and do **not** execute under Amix/SVR4. They are still useful to you in two ways: (1) the on-card **DIP switches and register/HW offsets** are hardware facts that apply identically under Amix, and (2) you can use a **driverless ROM** (or no ROM) so that the card AutoConfigs but does no AmigaOS SCSI scan — which is the closest analog to "silence the autoboot scan." Details below.

## 1. Toolchain + build commands + artifacts

**Toolchain required** (none of it is currently installed on this box — `m68k-amigaos-gcc`, `vlink`, `vasmm68k_mot` were all NOT FOUND; only `/usr/bin/lha` exists):
- `m68k-amigaos-gcc` + `m68k-amigaos-strip` — Bebbo's amiga-gcc cross toolchain (`https://github.com/reinauer/container-amiga-gcc`, README line 30). Makefile expects it on PATH or at `/opt/amiga/bin` (`Makefile:88-89,174`).
- `vlink` (ROM raw-binary link), `vasmm68k_mot` (assembles `rom.S`/`reloc.S`/`kickmodule.S`) — `Makefile:90-91`.
- Host `cc` (HOSTCC) — builds the host-side tools `ncr53cxxx` (SCRIPTS assembler) and `romtool` (`Makefile:87,269-271,312-314`).
- NDK includes auto-located at `$(dirname gcc)/../m68k-amigaos/ndk-include` (`Makefile:94`); override with `NDK_PATH=`.
- For the ADF/disk and the `lha` dist archive: `lha`, and the `disk/` subdir tooling.

**Commands** (run from `reference/a4091-software`):
```
make                          # default DEVICE=A4091 -> builds everything (all: $(PROG) $(ROMS) $(TOOLS))
make verbose                  # same, full compiler command echo (Makefile:151-158)
make DEVICE=A4091             # explicit; other targets: A4092, A4770, A4000T, A4000T770 (Makefile:12-50)
PATH=$PATH:/opt/amiga/bin make    # if toolchain lives in /opt/amiga (Makefile:174 hint)
make clean / make distclean   # Makefile:346-356
cd disk && make               # build bootable ADF floppy image (README:57-62)
make all-targets              # builds every board variant + debug ROMs + disk + lha (Makefile:384-420)
```
First `make` auto-runs `git submodule update --init --recursive` (`Makefile:177-182`) — needs network for the submodules (mounter, ODFileSystem, artwork, a4092flash).

**Artifacts** for `DEVICE=A4091` (`Makefile:54-61,208-215`, README:44-53):
| File | What |
|---|---|
| `a4091.device` | AmigaOS driver, ZX0-compressed into the ROM, also loadable from disk. ~44 KB (release v42.37). |
| `a4091.rom` | 64 KB AutoConfig ROM = `a4091_nodriver.rom` + ZX0(`a4091.device`) inserted by `romtool -D` (`Makefile:316-323`). |
| `a4091_nodriver.rom` | ROM header / AutoConfig only, **no driver** — "useful for diagnostics or loading the driver from disk" (README:51). This is the ROM that makes the card enumerate but not run the AmigaOS SCSI driver. |
| `a4091_cdfs.rom` | ROM + CDFileSystem for CD boot (`romtool -F … -T 0x43443031`, `Makefile:325-327`). |
| `ncr7xx` | hardware test/probe CLI (was `PROGU`). |
| `a4091d` | driver-state debug CLI (`PROGD`). |
| `objs-A4091/romtool` | host ROM build/inspect/patch tool (`Makefile:312`). |
| `a4091.kick` | only built for `A4000T`/`A4000T770` (HAVE_ROM=n path, `Makefile:208-213,330`). |

Key build defines for A4091 (`Makefile:18-23`): `-DDRIVER_A4091 -DNCR53C710=1 -DARCH_710 -DDEVNAME="a4091" -DHAVE_ROM=1`. The 53C710 SCRIPTS path uses `siop.c` + `siop_script.ss` assembled by the built `ncr53cxxx` into `siop_script.out` (`Makefile:64-66,162-163,227,261-263`). (The A4092/A4770 use `siop2.c`/`siop2_script.ss`.) `-mcpu=68060 -Os -fomit-frame-pointer -noixemul -msmall-code` (`Makefile:124-128`).

## 2. Runtime CONFIG / DEBUG levers

**There are NO runtime env vars for the driver.** A grep for `GetVar/getenv/ReadArgs` finds hits only in 3rdparty ODFileSystem/bffs, never in the A4091 driver. Configuration is **only** via DIP switches, battmem, and the boot menu.

**(a) Silence / disable the AmigaOS autoboot SCSI scan:**
- The actual bus scan is `scsi_probe_bus(sc,-1,-1)` at `scsiconf.c:380`, looping `target = mintarget..maxtarget` at `scsiconf.c:481` calling `scsi_probe_device` (`scsiconf.c:488`). The AmigaOS auto-mount that follows is `mount_drives()` → `MountDrive()` in `device.c:407-447`.
- To make the card AutoConfig but **not** drive the AmigaOS SCSI scan/mount, flash **`a4091_nodriver.rom`** (no embedded driver, README:51) — or run the card with **no ROM** at all. Either way the AmigaOS driver never attaches, so there's no AmigaOS scan competing with Amix. (Under Amix none of this runs regardless; this only matters if you dual-boot AmigaOS on the same machine.)
- Boot-menu / battmem knobs that influence the AmigaOS scan/mount (do nothing under Amix): `cdrom_boot`, `ignore_last` (ignore RDB `RDBFF_LAST` so a bad drive doesn't stop the scan — README:124), `allow_disc`, `quick_int`. Stored in battmem (`battmem.c`, addresses in `battmem.h:28-35`: CDROM_BOOT=72, IGNORE_LAST=73, QUICK_INT=74, ALLOW_DISC=75, 1 byte each). Note `cdrom_boot` is **stored inverted** (`battmem.c:110,150`).

**(b) DIP switches (hardware — apply under Amix too).** Read by `get_dip_switches()` at `attach.c:393-399` from `asave->as_addr + HW_OFFSET_SWITCHES`. For A4091 `HW_OFFSET_SWITCHES = 0x008c0003` (`a4091.h:21,65`) i.e. board+0x8C0003; there's also `A4091_OFFSET_QUICKINT = 0x00880003` and `A4091_OFFSET_REGISTERS = 0x00800000` (the 53C710 regs at board+0x800000 — matches your setup). Switch meanings (`bootmenu.c:dipswitch_text` 503-551, decoded in `attach.c` get_host_id/get_lun_count 449-477):
- SW1–3: **SCSI host adapter ID** (bits 0–2; `get_host_id` masks `dip & 7`).
- SW4: SCSI-1 Slow vs **SCSI-2 Fast** bus mode.
- SW5: Long vs **Short Spinup** (drives `ms.slowSpinup`, `device.c:436`).
- SW6: Asynchronous vs **Synchronous** SCSI mode.
- SW7: External Termination On/Off.
- SW8: **SCSI LUNs** Enabled/Disabled (drives `get_lun_count`, `device.c:435`).

The boot menu's **DIP Switch Viewer** (`dipswitch_page`, `bootmenu.c:670-691`) and **SCSI Device Summary / Disks** page (`scan_disks`, `bootmenu.c:796-972`, which does `OpenDevice` + `INQUIRY` + `READ CAPACITY` per target) are good ways to *read back* what the card sees from AmigaOS, if you boot AmigaOS for diagnosis. Boot menu is entered by holding **right mouse button or DEL** at power-up (`boot_menu`, `bootmenu.c:1765-1812`); it ends in `ColdReboot()` (`bootmenu.c:1844`).

**(c) Serial debug logging (compile-time, the real 53C710 lever).** Uncomment `-DDEBUG_*` lines in `Makefile:100-118`; output goes to serial **9600 8-N-1** (README:209-212). The directly 53C710-relevant flags:
- `-DDEBUG_SIOP` → `siop.c` (53C710 SCRIPTS/interrupt/phase logging) — **this is the one you want when bringing up the chip.**
- `-DDEBUG` (basic), `-DDEBUG_SYNC` (sync negotiation), `-DDEBUG_CMD`, `-DDEBUG_CALLOUT` (timeouts/abort), `-DDEBUG_ATTACH` (`attach.c` — reset/init), `-DDEBUG_DEVICE`, `-DDEBUG_SCSIPI`, `-DDEBUG_SCSIPI_BASE`, `-DDEBUG_SCSICONF` (bus probe), `-DDEBUG_SCSIMSG`, `-DDEBUG_SD`, `-DDEBUG_MOUNTER`, `-DDEBUG_BOOTMENU`.
- `-DNO_SERIAL_OUTPUT` turns all serial off. A prebuilt **`a4091_debug.rom`** is shipped per release, equivalent to `make DEBUG="-DDEBUG -DDEBUG_DEVICE -DDEBUG_SD -DDEBUG_MOUNTER"` (`Makefile:388,402`).

**(d) `a4091d` debug tool — capabilities.** It opens `a4091.device` with `TDF_DEBUG_OPEN` and dumps the live driver/chip state. The README's `d/u/o/c/a` flags are **stale**; the real argument parser (`a4091d.c:1684-1750`) is:
```
a4091d <unit>           # dump everything for that unit (unit number is required)
a4091d -c               # dump 68040 special regs: ITT0/1 DTT0/1 TC URP SRP VBR SFC DFC (a4091d.c:146-182)
a4091d -p <hexaddr>     # decode a scsipi_periph at address
a4091d -x <hexaddr>     # decode a scsipi_xfer (xs) at address
a4091d -w               # open device and wait (hold state open)
```
With a unit, it dumps (a4091d.c:1845-2044): the IORequest/Unit, the `scsipi_periph` (flags via `bits_periph_flags`, caps), the `scsipi_channel` (chan_id = SCSI host ID, periph hash table, queues `chan_queue`/`chan_complete`, free xs list), and the **`siop_softc`** — which is exactly the 53C710 state you care about: `sc_istat/sc_dstat/sc_sstat0/sc_sstat1` (`a4091d.c:1958`, the ARCH_710 branch), `sc_intcode`, `sc_scriptspa` (SCRIPTS phys addr), `sc_nexus`/`free_list`/`ready_list`/`nexus_list`, `sc_tinfo[]` per-target, `sc_clock_freq`, `sc_dcntl`/`sc_ctest7`, `sc_sync[]` (state/sxfer/sbcl), `sc_minsync`, `sc_sien`/`sc_dien`. It also has rich SCSI decoders (sense keys, ASC/ASCQ table, xs errors, command opcodes) you can lift for your own Amix-side logging.

**(e) `ncr7xx` hardware test.** `ncr7xx -t` runs the full register/data/address-pin/interrupt/SCSI-pin self-test; `-t56` runs only tests 5–6; `-t -L` loops for burn-in (README:163-185). This is a NetBSD-derived NCR53C7xx tester and is the most direct "does my chip respond / are my register accesses correct" check short of running the driver — directly relevant to validating your big-endian-lane, write-shadow-at-+0x40, 8/32-bit-only access pattern.

## 3. Pre-built RELEASES (yes — downloadable known-good ROMs)

GitHub releases exist and are current: `https://github.com/A4091/a4091-software/releases`. Latest is **v42.37 (2026-04-24)**; full history back to release_42.23 (2022). Each release ships ready-to-flash assets. v42.37 A4091 assets (verified via GitHub API):
- `a4091.rom` (65536 B) — full ROM with driver
- `a4091_nodriver.rom` (65536 B) — **driverless ROM** (AutoConfig only — your "no AmigaOS scan" option)
- `a4091_cdfs.rom`, `a4091_debug.rom` (serial-debug build), `a4091.device` (44104 B)
- `a4091_42.37.adf` (the bootable tools/diagnostics floppy with `ncr7xx`, `a4091d`)

So you can swap in a known-good ROM without building. Download e.g.:
```
gh release download v42.37 -R A4091/a4091-software -p 'a4091*.rom' -p 'a4091.device' -p 'a4091d'
```
Your local checkout currently has **no tags** (`git describe` empty, `git tag` empty — it's at loose commit `3428168 "3rdparty: Update TinySetPatch"`), so the build's `FULL_VERSION` would fall back oddly; if you build locally, fetch tags first (`git fetch --tags`) or pass `FULL_VERSION=42.37` to `make` so version.c/the ROM stamp is sane.

## 4. `romtool.c` — what it does (and re: disabling autoboot)

`romtool` (host C, built at `objs-A4091/romtool`, version "v0.4 2025-11-28") inspects and rewrites the **layout** of a ROM image. It is **not** a config editor — it cannot toggle autoboot/battmem/DIP behavior. What it does (`romtool.c`):
- **Inspect** (`inventory()`, 73-120): validates the 16-byte trailer signature `0xFFFF5352 0x2F434448` ("…SR/CDH", `romtool.c:78`), prints driver + up-to-two filesystem slots (offset/length/DosType), detects ZX0 (`0x5a583001`) / RNC (`0x524e4301`) compression (`is_compressed`, 63-71), and reports free space. Running `romtool a4091.rom` alone just prints this.
- **Replace driver** `-D <file>` (DEVICE slot, `romtool.c:485-489`) — this is how the Makefile injects the compressed driver (`Makefile:323`).
- **Replace/add filesystem** `-F <file> -T <dostype>` (slot 1), second `-F`/`-T` or `-s`+`-F2` for slot 2 (`romtool.c:407-427`); e.g. CDFileSystem `-T 0x43443031`, fat95 `-T 0x46415420` (README:231-263).
- **Resize** `-r 32|64` (`romtool.c:435-441,136-180`), `-o <out>`.

**Can it disable autoboot / build a minimal AutoConfig-only ROM?** Indirectly:
- It can produce a **driverless ROM** by replacing the DEVICE slot with an empty/zero-length file — but the supported and cleaner way is simply to use/flash **`a4091_nodriver.rom`** (the Makefile builds it as `$(ROM_ND)` via `vlink -Trom.ld -brawbin1` from just `rom.o`, `Makefile:316-318` — i.e. AutoConfig header + ROM bootloader, **no driver**). That ROM AutoConfigs the board but never runs the AmigaOS SCSI driver/scan. That's your "minimal AutoConfig-only ROM."
- It does **not** flip a battmem bit or set a "don't scan" flag — there is no such ROM-resident flag; autoboot/mount behavior is decided by the *driver* (`mount_drives` in `device.c`) using battmem + DIP, none of which `romtool` touches. The real autoboot kill-switch is "ship a ROM with no driver" (or no ROM, and load `a4091.device` from disk only when you want it).

## Bottom line for your 53C710 bring-up under Amix

- For chip bring-up reference, the authoritative code paths are `siop.c` + `siop_script.ss` (built via `ncr53cxxx` → `siop_script.out`), with reset/init in `attach.c` and register names in `siopreg.h`. Build with `-DDEBUG_SIOP -DDEBUG_ATTACH -DDEBUG_SYNC` and watch serial @9600 to see a known-good init/SELECT/INQUIRY sequence on the same hardware, which you can mirror in your Amix driver.
- HW offsets to trust (A4091): regs at board+`0x800000`, switches at board+`0x8c0003`, quickint at board+`0x880003` (`a4091.h:17-21`).
- For a stable AmigaOS-side baseline (so the card behaves and you can A/B against Amix), flash the prebuilt **v42.37 `a4091.rom`**, or **`a4091_nodriver.rom`** if you want the card present but silent on the AmigaOS side. Use **`ncr7xx -t`** to validate your register-access plumbing and **`a4091d <unit>`** to read live 53C710 state (`sc_istat/sc_dstat/sc_sstat0/sc_sstat1/sc_intcode/sc_scriptspa`).

Relevant absolute paths:
- `reference/a4091-software/Makefile`
- `reference/a4091-software/README.md`
- `reference/a4091-software/romtool.c`
- `reference/a4091-software/a4091d.c`
- `reference/a4091-software/bootmenu.c`
- `reference/a4091-software/battmem.c` and `battmem.h`
- `reference/a4091-software/attach.c` (DIP/host-id readers ~393-477; switch write ~359)
- `reference/a4091-software/device.c` (mount_drives 407-447)
- `reference/a4091-software/scsiconf.c` (scsi_probe_bus 380/449/481)
- `reference/a4091-software/a4091.h` (HW offsets 17-21)
- Releases: `https://github.com/A4091/a4091-software/releases` (latest v42.37, 2026-04-24).
