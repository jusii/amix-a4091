# A4091 + 53C710 driver reference (for Amix)

> Synthesised from 6 independent research agents (NCR 53C710 Jun-1992 Data Manual, NetBSD `osiop`/amiga `siop`, Linux `53c700`, QEMU `lsi53c710`, WinUAE/Amiberry `ncr_scsi.cpp`, the A4091/a4091-software driver), then **verified against primary sources** (OpenBSD/NetBSD `osiopreg.h`/`osiop.c`, the A4091 firmware `siop.c`, the QEMU 53C710 core, WinUAE `ncr_scsi.cpp`, MVME167 Table 3-10). Target: **Amiga UNIX (Amix) 2.1 = AT&T SVR4.0 on a 68030, big-endian, ILP32**, under **Amiberry 8.1.6**.
>
> **Confidence tags** `HIGH / MED / LOW` and the **source** are appended to every hex offset/number. Where sources disagree the conflict is kept **visible**, not silently resolved. Items the verifiers could not independently nail are flagged **LOW + "verify empirically against the live chip."**

---

## 0. Empirical ground truth (anchor facts)

These come from the running system and are trusted as ground truth; the rest of this document is cross-referenced against them.

| Fact | Value | Confidence | Source |
|---|---|---|---|
| Chip | NCR/Symbios **53C710** (NOT WD33C93A, NOT 53C720/8xx) | HIGH | empirical + Data Manual |
| Board | Commodore **A4091**, Zorro III, NCR 53C710 | HIGH | empirical |
| AutoConfig ID | mfg `0x0202` (Commodore), product `0x54`, er_Type `0x90` (Z3) | HIGH | empirical + a4091.h |
| Physical board base | `0x40000000` | HIGH | empirical |
| Board window size | `0x01000000` (16 MB) | HIGH | empirical + emulator BOARD_SIZE |
| Host CPU | 68030, big-endian, ILP32 | HIGH | empirical |
| Discovery API | `autocon(0x02020054, unit, &base, &size)` returns `cd_BoardAddr` = `0x40000000` (no Z2/Z3 filtering) | HIGH | empirical |
| Safe read | first 4 KB @ `0x40000000` -> AutoConfig nibble regs at offsets 0,4,…,0x1e | HIGH | empirical |
| Crash | reading the **whole 16 MB** window SIGSEGVs the Amiberry emulation | HIGH | empirical (see §7) |

---

## 1. AutoConfig & board memory map

The 16 MB Zorro III window is **sparse**: only four sub-regions are decoded. Two independent primary sources (a4091-software `a4091.h` and WinUAE/Amiberry `ncr_scsi.cpp`) agree **exactly**, and the register-block offset is additionally confirmed by the driver's `attach.c`.

### 1.1 Board sub-regions (offset from base; absolute = base + offset)

| Region | Board offset | Absolute @ `0x40000000` | Confidence | Source |
|---|---|---|---|---|
| AutoConfig nibble regs | `0x00000000` | `0x40000000` | **HIGH** | a4091.h `A4091_OFFSET_AUTOCONFIG`; emulator |
| Autoboot ROM (`a4091.device`) | `0x00000000` | `0x40000000` | **HIGH** | a4091.h `A4091_OFFSET_ROM`; emulator (decoded for addr < `0x00800000`) |
| **NCR 53C710 register block** | **`0x00800000`** | **`0x40800000`** | **HIGH** | a4091.h `A4091_OFFSET_REGISTERS`; `attach.c` `dev_base + HW_OFFSET_REGISTERS`; emulator `io_start`; NetBSD `afsc.c` `va+0x00800000` |
| 53C710 register alias (alt) | `0x00840000` | `0x40840000` | MED | emulator `A4091_IO_ALT` (real in the emulator; no driver-header counterpart, so MED for a real-hardware alias) |
| QUICKINT (IRQ-ack byte) | `0x00880003` | `0x40880003` | **HIGH** | a4091.h; **the shipped driver writes the hi-bank alias `0x008A0003`** (`A4091_OFFSET_QUICKINT \| (1<<17)`, ack byte `0x26`) — both decode to the same QUICKINT register |
| SWITCHES / DIP (byte) | `0x008C0003` | `0x408C0003` | **HIGH** | a4091.h; emulator `A4091_DIP_OFFSET` |

> **Cross-ref to empirical fact:** the user's `0x40000000` base + `0x00800000` register offset gives **`0x40800000`** — the single most important derived address for bring-up. HIGH (triple-sourced: a4091.h `A4091_OFFSET_REGISTERS`, `attach.c` `dev_base + HW_OFFSET_REGISTERS` / `a4091_release(... - 0x00800000)`, WinUAE `io_start`, plus NetBSD `afsc.c`).

### 1.2 ROM offset and size

| Item | Value | Confidence | Source |
|---|---|---|---|
| ROM offset within board | `0x00000000` | HIGH | a4091.h, emulator |
| ROM **decoded** region (emulator) | `0x000000`–`0x7FFFFF` (8 MB) | HIGH | emulator `rom_end = A4091_IO_OFFSET = 0x800000` |
| I/O window end (emulator) | `A4091_IO_END = 0x00880000` (I/O decoded `0x800000`–`0x87FFFF`) | HIGH | emulator `io_end` |
| ROM **image** size (emulator constant) | `A4091_ROM_SIZE = 65536` = `0x10000` (64 KB), read in 4-bit nibbles | HIGH | **`ncr_scsi.cpp` (emulator)** — *not* `a4091.h`; `rom.S` |
| Real-device EPROM capacity | 32 KB or 64 KB (v40.13/v42 images are 32768 bytes; Makefile accepts both) | HIGH | a4091-software Makefile/ROM images |
| ROM heap **allocation** in emulator | `0x40000` (256 KB) only | HIGH | WinUAE `xcalloc(A4091_ROM_SIZE*4)` = 65536×4 = `0x40000` |

> **Corrected attribution:** `A4091_ROM_SIZE` does **not** exist in `a4091.h` (checked on `main` and `master`); it is a **WinUAE/`ncr_scsi.cpp`** constant (`65536`). The 64 KB figure is the emulator constant / EPROM capacity, not a driver-header define.
>
> **Conflict / hazard (kept visible):** the emulator declares the ROM region as 8 MB but allocates only **256 KB**. This mismatch is the root cause of the full-window SIGSEGV — see §7.

### 1.3 Register-block width and mirroring
- 53C710 register file = **`0x40` bytes** (offsets `0x00`–`0x3F`). HIGH (Data Manual Table 4-1; emulator `io_mask=0x3f`; `OSIOP_NREGS=0x40`).
- In the emulator the region `0x800000`–`0x87FFFF` **mirrors** the 64-byte file every `0x40` (`reg = addr & 0x3f`). HIGH.

---

## 2. NCR 53C710 register map (full table)

### 2.1 The endianness question — READ THIS FIRST

The 53C710 is **internally always little-endian**; host endianness is set by an **external pin** (no software BE/LE bit). HIGH. For **single-byte registers** the byte address depends on endianness:

```
BE_offset = (LE_offset & ~3) + (3 - (LE_offset & 3))
```

This is algebraically identical to `osiopreg.h`'s byte-lane scheme `base + BL(3-lane)` (BL0=3, BL1=2, BL2=1, BL3=0), so the "LE" cross-reference column in Table 2.2 is internally consistent and correct.

**32-bit (longword) registers** (DSA, TEMP, DNAD, DSP, DSPS, SCRATCH, ADDER, DBC/DCMD longword) have the **same offset in both modes**, and **all SCRIPTS words/DMA addresses are longwords**, so they are byte-order transparent.

> **CRITICAL agreement that looks like a conflict — resolved:** Agents 1 & 4 call the A4091 map "**big-endian**" and tabulate `SCNTL0=0x03, SIEN=0x00, ISTAT=0x22, DSTAT=0x0F`. Agents 3 & 6 (NetBSD `osiopreg.h`/amiga `siopreg.h`, the **actual A4091 driver source**) call the same numbers the "**little-endian/right-justified**" map: `SCNTL0=0x03, SIEN=0x00, ISTAT=0x22, DSTAT=0x0F`. **The numeric offsets are IDENTICAL** — only the label differs (a naming clash between NCR datasheet columns and the NetBSD header). **Use Table 2.2; it is agreed by all four sources plus the emulator's `beswap`.** HIGH (all 40 offsets independently reconstructed from `osiopreg.h` and cross-checked against the NCR Jun-92 Data Manual and the VxWorks `ncr710Lib` dump — zero corrections).
>
> Agent 6 raised a MED caveat that *which* datasheet column applies is board-strap-dependent and "should be verified empirically (ISTAT at `0x22` vs `0x21`)." This is **resolved** by three concrete A4091/Amiga sources (NetBSD `siopreg.h`/`afsc.c`, A4091 firmware, WinUAE `beswap`) all using the same map. Verify on first contact, expect Table 2.2.

> **MVME167 is a CONFIRMING source, not an outlier (corrected):** an earlier draft flagged the MVME167 (68040 board) big-endian view (`SCNTL0=0x00, SIEN=0x10, DSTAT=0x0C…`) as "a different board's wiring" that "disagrees" with the A4091 map. That framing was **wrong**. MVME167 Table 3-10 lists registers by their *aligned-longword row* (row `0x00`→SCNTL0, row `0x04`→SCID, row `0x08`→SFBR, row `0x0C`→DSTAT, row `0x14`→CTEST4, row `0x1C`→DFIFO, row `0x38`→DMODE); resolving each to its byte lane (BL0=3) gives SCNTL0→`0x03`, DSTAT→`0x0F`, etc. — **identical to the A4091/`osiopreg.h` map**. There is no board-strap difference between MVME167 and A4091 for these offsets; both are big-endian 53C710 with the same byte-lane reversal. The `SCNTL0=0x00 / SIEN=0x10 / SDID=0x20` style numbers were a misread of a *word-index* table. **MVME167 corroborates §2.2.** HIGH.

### 2.2 Complete register map — A4091 offsets (use these)

`A4091 BE` = the byte offset to use on the 68030. `LE` = datasheet little-endian numbering (cross-ref to QEMU/Linux headers). Longword regs: A4091==LE. All HEX, relative to register base (`0x40800000`). **HIGH** throughout unless noted (NCR Data Manual Jun92 Table 4-1; all 40 offsets independently reconstructed from NetBSD/OpenBSD `osiopreg.h` and cross-validated against QEMU `ncr53c710.h`, Linux `53c7xx.h`, MVME167 Table 3-10, and the VxWorks `ncr710Lib` dump).

| LE | A4091 (BE) | R/W | Width | Name | Meaning |
|----|----|-----|-------|---------|---------|
| 00 | **03** | R/W | 8 | SCNTL0 | SCSI control 0 (arb, START, WATN, EPC, EPG, AAP, TRG) |
| 01 | **02** | R/W | 8 | SCNTL1 | SCSI control 1 (EXC, ADB, ESR, CON, RST/, AESP) |
| 02 | **01** | R/W | 8 | SDID | SCSI destination ID (one-hot) |
| 03 | **00** | R/W | 8 | SIEN | SCSI interrupt enable (mask of SSTAT0) |
| 04 | **07** | R/W | 8 | SCID | SCSI chip/own ID (one-hot) |
| 05 | **06** | R/W | 8 | SXFER | SCSI transfer (DHP, sync period, offset) |
| 06 | **05** | R/W | 8 | SODL | SCSI output data latch |
| 07 | **04** | R/W | 8 | SOCL | SCSI output control latch |
| 08 | **0B** | R/W*| 8 | SFBR | SCSI first byte received |
| 09 | **0A** | R | 8 | SIDL | SCSI input data latch |
| 0A | **09** | R | 8 | SBDL | SCSI bus data lines (live) |
| 0B | **08** | R/W | 8 | SBCL | SCSI bus control lines (live) |
| 0C | **0F** | R | 8 | DSTAT | DMA status (DMA-side cause) |
| 0D | **0E** | R | 8 | SSTAT0 | SCSI status 0 (SCSI-side cause) |
| 0E | **0D** | R | 8 | SSTAT1 | SCSI status 1 (FIFO flags, arb phase) |
| 0F | **0C** | R | 8 | SSTAT2 | SCSI status 2 (FIFO count, latched phase) |
| 10-13 | **10-13** | R/W | 32 | DSA | Data Structure Address |
| 14 | **17** | R/W | 8 | CTEST0 | Chip test 0 |
| 15 | **16** | R | 8 | CTEST1 | Chip test 1 |
| 16 | **15** | R | 8 | CTEST2 | Chip test 2 (SIGP, DREQ/DACK) |
| 17 | **14** | R | 8 | CTEST3 | Chip test 3 (top of SCSI FIFO) |
| 18 | **1B** | R/W | 8 | CTEST4 | Chip test 4 |
| 19 | **1A** | R/W | 8 | CTEST5 | Chip test 5 |
| 1A | **19** | R/W | 8 | CTEST6 | Chip test 6 |
| 1B | **18** | R/W | 8 | CTEST7 | Chip test 7 (CDIS, SC1/SC0, DFP, EVP, TT1, DIFF) |
| 1C-1F | **1C-1F** | R/W | 32 | TEMP | Temp/return-address stack |
| 20 | **23** | R/W | 8 | DFIFO | DMA FIFO (count/flush) |
| 21 | **22** | R/W | 8 | ISTAT | **Interrupt status — only host-safe poll; soft-reset bit** |
| 22 | **21** | R/W | 8 | CTEST8 | Chip test 8 (rev V3-0, FLF, CLF, FM, SM) |
| 23 | **20** | R/W | 8 | LCRC | Longitudinal parity / CRC (see conflict) |
| 24-26 | **25-27** | R/W | 24 | DBC | DMA byte counter |
| 27 | **24** | R/W | 8 | DCMD | DMA command (SCRIPTS opcode byte) |
| 28-2B | **28-2B** | R/W | 32 | DNAD | DMA next address (working ptr) |
| 2C-2F | **2C-2F** | R/W | 32 | DSP | **DMA SCRIPTS pointer — WRITE starts SCRIPTS** |
| 30-33 | **30-33** | R/W | 32 | DSPS | SCRIPTS ptr save / instr word2 / INT vector |
| 34-37 | **34-37** | R/W | 32 | SCRATCH | scratch |
| 38 | **3B** | R/W | 8 | DMODE | DMA mode (burst, FC, PD, FAM, MAN) |
| 39 | **3A** | R/W | 8 | DIEN | DMA interrupt enable |
| 3A | **39** | R/W | 8 | DWT | DMA watchdog (16×BCLK; 0=off) |
| 3B | **38** | R/W | 8 | DCNTL | DMA control (CF, EA, SSM, LLM, STD, FA, COM) |
| 3C-3F | **3C-3F** | R | 32 | ADDER | adder output (diagnostic) |

**NREGS = `0x40` (`OSIOP_NREGS`).** Host accesses must be **8-bit or 32-bit only — NEVER 16-bit** (HIGH — Data Manual + MVME167 note).

> **DCMD/DBC byte order — the one spot where BE byte order is load-bearing (HIGH):** `osiopreg.h` defines a single 32-bit word at `0x24`: `DCMD` = `0x24+BL3` = byte **`0x24`** in BE (the most-significant byte / bits 31-24), and the 24-bit `DBC` count occupies the lower 3 bytes = BE addresses **`0x25`–`0x27`**. Anyone hand-poking `DBC` must write the 24-bit count to **`0x25`–`0x27`**, not `0x24`. Consistent with the SCRIPTS word-1 format (§5.1).

> **Conflict — LE 0x23 (A4091 BE 0x20):** Linux `53c7xx.h` labels it `CTEST9`; NCR Data Manual and `osiopreg.h` (`OSIOP_LCRC` at `0x20+BL3`) label it **`LCRC`**. Use **LCRC** on the 710; "CTEST9" is Linux-header naming for a later chip. MED.

> **Registers that DO NOT EXIST on the 53C710 — do not probe:** `SIST/SIST0/1, SLPAR, MACNTL, GPCNTL, GPREG, STIME0/1, RESPID, STEST0..3, SCNTL2/3, SIEN0/1, DSAREL`. These are 53C720/770/8xx. The 710 file is exactly `0x00`–`0x3F`. HIGH (probing them hits the mirrored 64-byte file at unexpected offsets).

### 2.3 Bit definitions (mask = value); offsets as A4091 BE

```
ISTAT (BE 22): ABRT=0x80 RST=0x40 SIGP=0x20 CON=0x08 SIP=0x02 DIP=0x01
DSTAT (BE 0F): DFE=0x80(default 1) BF=0x20 ABRT=0x10 SSI=0x08 SIR=0x04 WTD=0x02 IID=0x01
SSTAT0(BE 0E): MA=0x80 FCMP=0x40 STO=0x20 SEL=0x10 SGE=0x08 UDC=0x04 RST=0x02 PAR=0x01
SSTAT1(BE 0D): ILF=0x80 ORF=0x40 OLF=0x20 AIP=0x10 LOA=0x08 WOA=0x04 RST=0x02 SDP=0x01
SSTAT2(BE 0C): FF[3:0]=0xF0 SDP=0x08 MSG=0x04 C/D=0x02 I/O=0x01
SIEN  (BE 00): same layout as SSTAT0 (mask); non-fatal = SEL, FCMP
SCNTL0(BE 03): ARB1=0x80 ARB0=0x40 (FULL=0xC0) START=0x20 WATN=0x10 EPC=0x08 EPG=0x04 AAP=0x02 TRG=0x01
SCNTL1(BE 02): EXC=0x80 ADB=0x40 ESR=0x20 CON=0x10 RST=0x08 AESP=0x04
DMODE (BE 3B): BL[1:0]=0xC0 (00=1,01=2,10=4,11=8) FC[1:0]=0x30 (FC2=0x20 FC1=0x10) PD=0x08 FAM=0x04 UO/TIO=0x02 MAN=0x01
DIEN  (BE 3A): BF=0x20 ABRT=0x10 SSI=0x08 SIR=0x04 WTD=0x02 IID=0x01 — all DMA ints fatal regardless of mask
DCNTL (BE 38): CF1=0x80 CF0=0x40 EA=0x20 SSM=0x10 LLM=0x08 STD=0x04 FA=0x02 COM=0x01
               CF1:CF0 -> 00:SCLK/1(16.67-25) 01:SCLK/1.5(25-37.5) 10:SCLK/2(37.5-50) 11:SCLK/3(50-66.67)
CTEST7(BE 18): CDIS=0x80 SC1=0x40 SC0=0x20 STD=0x10 DFP=0x08 EVP=0x04 TT1=0x02 DIFF=0x01
CTEST8(BE 21): V[3:0]=0xF0 (rev, read-only) FLF=0x08 CLF=0x04 FM=0x02 SM=0x01
```
(All HIGH — Data Manual diagrams, confirmed bit-for-bit against NetBSD `osiopreg.h`. `DMODE_FC=0x30` is a 2-bit field; CTEST7 `STD=0x10` added for completeness.)

> **DCNTL prescale (CF) terminology:** NetBSD phrases it by SCSI clock: `<=25MHz->0x80 (SCLK/1)`, `<=37.5->0x40 (SCLK/1.5)`, `<=50->0x00 (SCLK/2)`, `else->0xC0 (SCLK/3)`. **For the A4091 (50 MHz, §6): `CF=0x00`.** HIGH (confirmed `osiop.c`: `freq<=50 → CF_2 (0x00)`).

---

## 3. Chip soft-reset + initialization sequence (ordered)

**Soft reset is via ISTAT, NOT DCNTL** on the 53C710 (the 700 resets via DCNTL; the 720/8xx via a different SRST — do not copy those). HIGH (NetBSD `osiop_reset`, A4091 firmware `siop.c`, Linux `53c700.c`, Data Manual ISTAT bit 6). Reset sets all regs to defaults, deasserts SCSI signals, does **NOT** assert SCSI RST/, **preserves `DCNTL.EA (0x20)` and FC1**, and is **NOT self-clearing**.

> **Reset write style (HIGH):** the real drivers use read-modify-write (`ISTAT |= ABRT; ISTAT |= RST; delay; ISTAT &= ~RST`) rather than the bare `=0x80/=0x40/=0x00` shown below. RMW is the safer pattern; the bare writes are functionally equivalent only right after power-on.

### 3.1 Concrete ordered sequence (A4091 BE; `R = 0x40800000`)

The write order below has been **corrected to match the authoritative `osiop.c`/firmware order**: SCNTL0 → SCNTL1 → DCNTL → DMODE → **ints off (SIEN=0, DIEN=0)** → SCID → DWT → **CTEST0** → **CTEST8 CLF FIFO-flush pulse** → CTEST7 → clear stale interrupts → enable interrupts. (An earlier draft put SIEN/DIEN after SCID/DWT and CTEST7 in the middle, and omitted CTEST0 and the CTEST8 CLF pulse.)

```c
/* ---------- soft reset (710: via ISTAT; RMW in real drivers) ---------- */
writeb(R + 0x22, 0x80);            /* (1) ISTAT.ABRT  : abort any running SCRIPT  */
writeb(R + 0x22, 0x40);            /* (2) ISTAT.RST=1 : assert chip software reset */
delay(100us);
writeb(R + 0x22, 0x00);            /* (3) ISTAT.RST=0 : release reset (NOT self-clearing) */
delay(100us);
/* ---------- configuration (SCSI regs lost on reset/power-up) ---------- */
writeb(R + 0x03, 0xCC);            /* (4) SCNTL0 = ARB_FULL(0xC0)|EPC(0x08)|EPG(0x04) */
writeb(R + 0x02, 0x20);            /* (5) SCNTL1 = ESR(0x20)  */
writeb(R + 0x38, 0x20);            /* (6) DCNTL  = CF=0x00 (SCLK/2 @50MHz) | EA(0x20) */
writeb(R + 0x3B, 0x80);            /* (7) DMODE  = BL4(0x80) burst length = 4 */
writeb(R + 0x00, 0x00);            /* (8) SIEN   = 0 (ints off before SCID/DWT) */
writeb(R + 0x3A, 0x00);            /* (9) DIEN   = 0 */
writeb(R + 0x07, (1 << own_id));   /* (10) SCID  = own ID one-hot (id7 -> 0x80) */
writeb(R + 0x39, 0x00);            /* (11) DWT   = 0 (watchdog off) */
writeb(R + 0x17, /*BTD|EAN*/0x00); /* (12) CTEST0 = NetBSD amiga path |= BTD|EAN (verify mask) */
writeb(R + 0x21, 0x04);            /* (13) CTEST8 |= CLF(0x04) : flush DMA/SCSI FIFO ... */
writeb(R + 0x21, 0x00);            /*      ... then clear CLF                              */
writeb(R + 0x18, 0x80);            /* (14) CTEST7 = CDIS(0x80) cache-burst disable (written last) */
/* ---------- clear stale interrupts (read-to-clear) ---------- */
(void)readb(R + 0x0E); (void)readb(R + 0x0D); (void)readb(R + 0x0F);  /* (15-17) SSTAT0,SSTAT1,DSTAT */
/* ---------- enable interrupts ---------- */
writeb(R + 0x00, 0xAF);            /* (18) SIEN = MA|STO|SGE|UDC|RST|PAR */
writeb(R + 0x3A, 0x35);            /* (19) DIEN = BF|ABRT|SIR|IID */
```
Values are NetBSD `osiop` (53C710) constants (HIGH). Notes:
- **Order is load-bearing:** DCNTL/DMODE early, interrupts off (SIEN=0/DIEN=0) before SCID/DWT, **CTEST7 written last**. The CTEST8 CLF pulse and the CTEST0 write are present in both NetBSD and the firmware; do not drop them.
- **CTEST0 mask** (`BTD|EAN` in NetBSD's amiga path) — **LOW: verify the exact mask empirically against the live chip / your `osiopreg.h`** before trusting the literal; the *presence* of the write is HIGH, the literal value here is a placeholder.
- If bidirectional STERM/TA is wanted, **`DCNTL.EA` must be the first I/O written** after reset (HIGH). Step (6) is before any SCSI op — acceptable; hoist it earlier if TA timing issues appear.
- `CF` must match the SCSI clock before SCSI ops; A4091 @ 50 MHz -> `CF=0x00`. HIGH.

> **Conflict — init *values* (corrected, kept visible):** the **A4091 firmware** (`a4091-software/siop.c`) differs from NetBSD in **fewer** places than an earlier draft claimed. Verified against the actual firmware:
> - `DMODE = 0xE0` on Amiga (burst-8 + drive FC2) vs NetBSD's conservative `0x80` (burst-4). **This is a genuine difference.** HIGH.
> - `CTEST8 |= SM (0x01)` (set-mode), **not `0x80`** (the earlier "CTEST8=0x80" was wrong; `0x80` is not even a defined CTEST8 bit — the top nibble is the read-only revision). HIGH.
> - `SIEN = 0xAF` and `DIEN = 0x35` in the firmware — **identical to NetBSD** (the earlier "SIEN=0x8F, DIEN=0x55, WTD on" was wrong; WTD is **not** enabled). HIGH.
>
> So the only genuine NetBSD-vs-firmware init divergence is **DMODE (`0x80` vs `0xE0`)** plus the **CTEST8 SM bit**. **Use the NetBSD conservative values for first bring-up** (`DMODE=0x80`, `CTEST7=0x80/CDIS set`), switch to the firmware `DMODE=0xE0` later. The choice is policy; both work. Do not mix bit-numbering conventions between the two headers.

> **A4091 Zorro-III write-cache workaround (HIGH, `attach.c`):** the driver keeps a **separate shadow write pointer at register-base + `0x40`** (`rp_write = (siop_regmap_p)((char *)rp + 0x40)` on A4091/A4092). Reads use `+0x00`. Account for this if register writes appear silently dropped.

---

## 4. Interrupt model (ISTAT / DSTAT / SSTAT0 / SSTAT1 / SSTAT2 decode)

### 4.1 Rules (HIGH — Data Manual + NetBSD + Linux agree)
- **ISTAT (BE `0x22`) is the only register safely accessible while SCRIPTS run.**
- `ISTAT.SIP (0x02)` -> read **SSTAT0** to decode; `ISTAT.DIP (0x01)` -> read **DSTAT**.
- Reading a status register **clears** the cause and **unstacks** the next; interrupts **stack** while SIP/DIP set.
- **Race-safety:** if both may be set, either read **SSTAT0 BEFORE DSTAT** with **≥12 BCLK** delay (Linux `udelay(10)`), or read the **32-bit longword at LE `0x0C`** (DSTAT|SSTAT0|SSTAT1|SSTAT2) in one access.

### 4.2 Service order (NetBSD `osiop_intr` / Linux `NCR_700_intr`)
```c
istat = readb(R + 0x22);                       /* read once */
if (!(istat & (0x02 | 0x01))) return NOT_OURS; /* SIP|DIP : neither -> not ours */
if (istat & 0x02) { delay(~10us); sstat0 = readb(R + 0x0E); }  /* SCSI -> SSTAT0 */
if (istat & 0x01) { delay(~10us); dstat  = readb(R + 0x0F); }  /* DMA  -> DSTAT  */
dsps = readl(R + 0x30);   /* SCRIPTS INT vector */
dsp  = readl(R + 0x2C);   /* resume address */
/* IRQ-ack: writeb(a4091_base + 0x8A0003, 0x26);  (QUICKINT hi-bank alias, as shipped) */
```

### 4.3 Decode tables

**ISTAT (BE 22):** ABRT 0x80 · RST 0x40 · SIGP 0x20 · CON 0x08 · **SIP 0x02 (->SSTAT0)** · **DIP 0x01 (->DSTAT)**.

**DSTAT (BE 0F) — DMA causes (read-to-clear; all fatal):** DFE 0x80 (status) · **BF 0x20 (bus fault — bad phys addr)** · ABRT 0x10 · SSI 0x08 · **SIR 0x04 (SCRIPTS INT = normal "done")** · WTD 0x02 · **IID 0x01 (illegal instr, e.g. DBC=0)**.

**SSTAT0 (BE 0E) — SCSI causes (SIEN masks bit-for-bit):** **MA 0x80 (phase mismatch)** · FCMP 0x40 · **STO 0x20 (sel/resel timeout)** · SEL 0x10 · SGE 0x08 · **UDC 0x04 (unexpected disconnect)** · RST 0x02 · PAR 0x01.

**SSTAT1 (BE 0D) — status:** ILF 0x80 · ORF 0x40 · OLF 0x20 · AIP 0x10 · LOA 0x08 · WOA 0x04 · RST 0x02 · SDP 0x01.

**SSTAT2 (BE 0C) — status:** FF[3:0] 0xF0 (SCSI-FIFO bytes 0–8) · SDP 0x08 · MSG 0x04 · C/D 0x02 · I/O 0x01 (latched phase).

### 4.4 Software abort (HIGH)
```
1) writeb(ISTAT,0x80);  2) wait IRQ;  3) istat=readb(ISTAT);
4) if (istat & SIP) { read SSTAT0; goto 2; }
5) if (!(istat&SIP) && (istat&DIP)) writeb(ISTAT,0x00);   /* clear ABRT before DSTAT */
6) read DSTAT -> confirm ABRT(0x10) + other causes
```

### 4.5 A4091 interrupt wiring
- Amiga **IRQ 3**, server priority **30**. HIGH (a4091.h `A4091_IRQ=3`, `A4091_INTPRI=30`).
- Ack: the shipped driver writes ack byte **`0x26`** to the **QUICKINT hi-bank alias `0x408A0003`** (`A4091_OFFSET_QUICKINT | (1<<17)`); the base form `0x40880003` decodes to the same QUICKINT register in the emulator. HIGH (`attach.c`).

---

## 5. SCRIPTS model + minimal INQUIRY program (annotated)

### 5.1 Execution model (HIGH — Data Manual Ch.5, confirmed QEMU/NetBSD/Linux)
On-chip **SCRIPTS processor** DMA-fetches 2- or 3-longword instructions from **host RAM** and runs until INT/error.
- **Encoding:** word1 -> `DCMD` (bits 31-24) + `DBC` (low 24); word2 -> `DSPS` (addr/vector); word3 (Memory Move only) -> dest (via TEMP).
- **Class** = DCMD bits 31-30: `00`=Block Move, `01`=I/O (SELECT/WAIT/SET/CLEAR; `101`-`111`=Read/Write reg), `10`=Transfer Control (JUMP/CALL/RETURN/INT), `11`=Memory Move. (Class = `insn>>30`, confirmed QEMU 710.)
- **Start:** write 32-bit physical start address into **DSP (`0x2C`)**; with `DMODE.MAN`(0x01) & `DCNTL.SSM`(0x10) clear it auto-starts. Byte-at-a-time: top byte (`0x2F`) is the trigger. HIGH.
- **On INT:** chip halts; vector in **DSPS (`0x30`)**; re-write DSP to resume.

### 5.2 Phase encoding (DCMD bits 26-24)
`000=DATA_OUT 001=DATA_IN 010=COMMAND 011=STATUS 110=MSG_OUT 111=MSG_IN`. Direct Block-Move DCMD top byte: DATA_OUT=`0x00`, DATA_IN=`0x01`, COMMAND=`0x02`, STATUS=`0x03`, MSG_OUT=`0x06`, MSG_IN=`0x07`. HIGH.

> **Conflict — Block-Move opcode top byte — RESOLVED (HIGH):** Agent 2 uses the bare phase byte (`0x06/0x02/0x01/0x03/0x07`); agent 6 ORed in MOVE-opcode bit27 (`0x0E/0x0A/0x09/0x0B/0x0F`). The QEMU **53C710** model matches a block-move phase on `(insn>>24)&7` with **no bit-27 dependency**, so for the 53C710 in **initiator** mode **bit27 = 0 and agent 2's values are correct.** (The earlier MED tag is upgraded to HIGH now that the QEMU 710 decode confirms it. Still worth a glance at your assembled `.scr` — expect bit27=0.)

### 5.3 Key SCRIPTS opcodes (word1), HIGH unless noted
```
SELECT ATN, target T  = 0x41000000 | ((1<<T) << 16)   ; ID is ONE-HOT, not binary!
        target0=0x41010000  target2=0x41040000  target6=0x41400000
WAIT DISCONNECT       = 0x48000000
SET ATN  = 0x58000008      CLEAR ATN = 0x60000008
INT (uncond.)         = 0x98080000   ; word2 = INT vector, readable in DSPS
RETURN (uncond.)      = 0x90080000   ; CALL = JUMP-family + 0x08000000
JUMP WHEN <phase>     = DATA_OUT 0x800B0000  DATA_IN 0x810B0000  CMD 0x820B0000
                        STATUS 0x830B0000  MSG_OUT 0x860B0000  MSG_IN 0x870B0000
```
Opcode subfield = `(insn>>27)&7`; class = `insn>>30`. Verified decodes: SELECT `0x41…` = class 1/opcode 0 (SELECT); WAIT DISCONNECT `0x48000000` = class 1/opcode 1 (DISCONNECT); INT `0x98080000` = class 2/opcode 3 (INT); RETURN `0x90080000` = class 2/opcode 2 (RETURN). HIGH.

> **SELECT destination-ID is ONE-HOT (HIGH — the key anti-conflation finding):** word1 bits 23-16 are **one-hot** (bit N => target N), not binary. The A4091 firmware builds it as `acb->ds.scsi_addr = (0x10000 << target)` = `1 << (16+target)`; the QEMU **710** model reads `(insn>>16)&0xff` then converts one-hot→binary via `idbitstonum() = 7 - clz8(id)`. **DANGER:** QEMU's `lsi53c895a.c` (8xx family) treats this same field as a plain 4-bit *binary* value — cross-referencing the 8xx model or an 8xx datasheet gives the **wrong** encoding. This is exactly the 53C710-vs-53C8xx conflation to avoid; use the one-hot form above.

> **LOW — INT/RETURN low flag bits (verify empirically against the live chip):** the class/opcode of INT (`0x98…`) and RETURN (`0x90…`) are confirmed, but whether the `0x080000` (bit-19) flag in `0x98080000`/`0x90080000` is mandatory vs. the plain `0x98000000`/`0x90000000` halting forms is **not** independently nailed. QEMU keys interrupt-on-the-fly vs. halt on bit-20 (`insn & (1<<20)`). **Verify against your assembled `.out`** before committing the literal; for a plain halting INT, `0x98000000` may suffice.

### 5.4 Host byte buffers (byte-linear, NO swap on the 68030)
```
IDENT  : 1 byte  = 0xC0 | LUN     ; IDENTIFY(0x80)|DiscPriv(0x40)|LUN; use 0x80|LUN to forbid disconnect
CDB    : 6 bytes = 12 00 00 00 24 00   ; INQUIRY 0x12, alloc length 0x24=36
DATAIN : 36 bytes ;  STATUS : 1 byte ;  MSGIN : 1 byte (expect 0x00 = COMMAND COMPLETE)
```
HIGH. SCSI payload bytes are never swapped (bus byte 0 -> buffer addr 0).

### 5.5 Minimal INQUIRY SCRIPTS program (Direct addressing; store each longword big-endian)
Each line = one 8-byte instruction: `word1 (DCMD|DBC)` then `word2 (DSPS = phys addr)`. Fast-RAM phys == bus phys (no IOMMU). HIGH structure (Linux `53c700.scr`, NetBSD `oosiop.ss`/`siop.ss`). Block-move top byte uses bit27=0 (§5.2, confirmed).
```
; word1 (DCMD|DBC)   word2 (DSPS phys)        ; mnemonic
0x41010000           <addr error_resel>       ; SELECT ATN, target 0 (one-hot; word2 = jump-on-reselect)
0x06000001           <phys IDENT>             ; MOVE 1,  IDENT,  WHEN MSG_OUT
0x02000006           <phys CDB>               ; MOVE 6,  CDB,    WHEN COMMAND
0x01000024           <phys DATAIN>            ; MOVE 36, DATAIN, WHEN DATA_IN   (0x24=36)
0x03000001           <phys STATUS>            ; MOVE 1,  STATUS, WHEN STATUS
0x07000001           <phys MSGIN>             ; MOVE 1,  MSGIN,  WHEN MSG_IN    (expect 0x00)
0x48000000           0x00000000               ; WAIT DISCONNECT
0x98080000           0xBEEF0000               ; INT done (halts; vector readable in DSPS) — see LOW note §5.3 on 0x98080000 vs 0x98000000
```
Hardening (after bare path works): JUMP-WHEN-`<phase>` dispatch before each MOVE; CLEAR ATN (`0x60000008`) after IDENTIFY; never emit a zero-length MOVE (`DBC=0` -> `DSTAT.IID`). Per-command patch points = the eight word2 addresses + four byte counts.

---

## 6. DMA / bus-master model on 68030 big-endian; minimal-INQUIRY data path

### 6.1 DMA is unavoidable (HIGH)
Even fetching each SCRIPTS instruction is a bus-master read at phys DSP, and INQUIRY data-in is a Block Move mastering the bus into host RAM. The only no-DMA path, **`DCNTL.LLM` (0x08, low-level mode)**, disables all SCRIPTS *and* DMA (test-only). Give the chip real Fast-RAM physical addresses and let it run.

### 6.2 Physical addressing (HIGH)
DMA registers are 32-bit physical, byte-addressed: `DSA`(0x10), `DSP`(0x2C), `DBC`(0x24-26, 24-bit; BE bytes `0x25`-`0x27`), `DNAD`(0x28). A4091 has no IOMMU: **CPU-physical == bus-physical**. `DBC` max = `0xFFFFFF`; 36 = `0x000024`; `DBC=0` is illegal.

### 6.3 Big-endian specifics (HIGH)
- **Longwords are byte-order transparent** — write SCRIPTS words and 32-bit addresses/counts as natural big-endian u_int32; **do NOT byte-swap**. Endianness only repositions byte-wide *register* addresses (Table 2.2 handles this).
- **SCSI payload bytes are never swapped** — the 36-byte buffer needs no swizzle.

### 6.4 Cache coherency (HIGH — chip bypasses the 68030 cache)
- **Before** DSP write: flush dirty lines for the data structure, SCRIPTS, CDB, IDENTIFY (and write buffers).
- **After** completion, **before** reading: invalidate the DATA_IN buffer + STATUS/MSGIN. (NetBSD: `dma_cachectl()` + `DCIAS(phys)`.)
- **Simplest 68030 approach:** put the data structure, SCRIPTS, CDB and 36-byte buffer in **cache-inhibited pages** (one small CI page holds the whole footprint), or flush+invalidate around the run.

### 6.5 Bus-fault — chip-side analog of the host SIGSEGV (HIGH)
A master cycle ending in BERR/TEA sets **`DSTAT.BF` (0x20)** and a fatal DMA interrupt. Keep DMA targets in **real Fast RAM** and register accesses **within `0x40800000`**. `DWT` (16×BCLK; 0=off) times out missing TA.

### 6.6 Addressing modes
- **Direct** (used in §5.5): instruction carries `[24-bit count | 32-bit absolute buffer phys]`; `DCMD` bit29=0, bit28=0. Simplest for one contiguous 36-byte buffer. HIGH.
- **Table-Indirect** (`DCMD` bit28=1): `DBC` = signed displacement from `DSA`; count+addr fetched from the DSA-relative table. Used by NetBSD for scatter/gather. **Not required** for minimal INQUIRY. HIGH.
- **Launch:** `DSA = phys(data structure)` (table-indirect only); `DSP = phys(SCRIPTS start)` starts execution. HIGH.

---

## 7. Amiberry / WinUAE A4091 emulation caveats

Files: **`ncr_scsi.cpp`** (board decode) wrapping **`qemuvga/lsi53c710.cpp`** (chip model). HIGH.

### 7.1 Safe vs unsafe offsets (relative to base `0x40000000`)

| Board offset | Region | Safe? | Why |
|---|---|---|---|
| `0x000000`–`0x0003FF` (~1 KB) | AutoConfig nibble regs | **YES** | real allocated data (what the user already reads) |
| `0x000400`–`0x03FFFF` | rest of real 256 KB ROM buffer | YES | within `xcalloc` 0x40000 |
| `0x040000`–`0x7FFFFF` | declared-ROM, **UNBACKED** | **NO** | `rom[]` indexed past 256 KB heap -> leak then SIGSEGV |
| **`0x800000`–`0x87FFFF`** | **53C710 register window** (`io_start=0x800000`, `io_end=A4091_IO_END=0x880000`) | **YES** | `reg = addr & 0x3f`, `beswap` lane; 64-byte file mirrored |
| `0x880000`–`0x8BFFFF` | gap | YES (reads 0) | outside I/O window |
| `0x8C0003` | DIP / SCSI-ID byte | YES | special-cased |
| up to `0xFFFFFF` | rest of window | mixed | `board_mask=0x00FFFFFF`; ROM range still unchecked |

(All HIGH — WinUAE decode; upstream identical.)

### 7.2 Why the full-window read SIGSEGVs (HIGH — exact root cause, verified verbatim)
The window is a **sparse function-decoded I/O bank** (`ncr_bank_generic`), **not** a flat buffer. ROM region declared `0`–`0x800000` (8 MB) but `ncr->rom = xcalloc(uae_u8, A4091_ROM_SIZE*4)` with `A4091_ROM_SIZE=65536` = `0x40000` (256 KB). `read_rombyte()` is literally `{ uae_u8 v = ncr->rom[addr]; return v; }` with **no bounds check**. A 16 MB sweep walks `addr` toward `0x800000`, ~8 MB past a 256 KB allocation, until an unmapped host page -> **host-process SIGSEGV** (not a guest bus fault). Therefore:
- **Map/read only the four decoded sub-regions**, never the full window.
- For the user's purpose: map a **small window at `0x40800000`** (register file; `addr & 0x3f`) and the **first ~4 KB at `0x40000000`** for AutoConfig — both backed and safe.

### 7.3 Emulator behaviour to know
- Register addressing `reg = (offset) & 0x3f`, lane `3 - (addr & 3)` (`beswap`) — matches Table 2.2. HIGH.
- `io_mask=0x3f`, `board_mask=0x00FFFFFF`, `io_start=0x800000`, `io_end=A4091_IO_END=0x880000`. HIGH. Register window mirrors every `0x40` through `0x87FFFF`; alias at `0x840000` (`A4091_IO_ALT`, real in the emulator; MED for real-hardware). AutoConfig nibbles: pre-config from `acmemory[]`, post-config from `rom[]`. HIGH.

---

## 8. Recommended incremental bring-up plan for Amix

Each step gates the next.

**Step 0 — Detect.** `autocon(0x02020054, unit, &base, &size)` -> expect `base=0x40000000`, `size=0x01000000`. HIGH (empirical).

**Step 1 — Map registers ONLY (avoid the SIGSEGV).** mmap `/dev/mem` for the register sub-region at **`base + 0x00800000` = `0x40800000`** (a single page; emulator mirrors `&0x3f`). Keep the safe first ~4 KB at `0x40000000` for AutoConfig. **Never map the whole 16 MB.** HIGH. Optional sanity: nibble regs at `0x40000000`+0,4,…,0x1e; DIP byte at `0x408C0003`.

**Step 2 — Soft reset.** §3.1 (1)–(3): `ISTAT(0x22)=0x80,=0x40,delay,=0x00,delay` (RMW form in production). Via **ISTAT**, not DCNTL. HIGH. Remember the **A4091 write-shadow at reg-base+`0x40`** if writes seem ignored.

**Step 3 — Read chip ID/revision (cheapest liveness check).** Read **`CTEST8` (BE `0x21`)**: top nibble `V[3:0]` (`0xF0`) = revision (manual documents rev 1). A sane non-`0xFF`/non-`0x00` nibble confirms the register block responds at `0x40800000` with correct byte lanes. Also read `ISTAT` at **`0x22`** to confirm the A4091-BE map empirically (agent-6 MED caveat). HIGH.

**Step 4 — Full init.** §3.1 (4)–(19) with **conservative NetBSD values** (`SCNTL0=0xCC, SCNTL1=0x20, DCNTL=0x20, DMODE=0x80, CTEST7=0x80, SCID=1<<id, DWT=0, SIEN=0xAF, DIEN=0x35`; 50 MHz -> `CF=0x00`) and the corrected write order (DCNTL/DMODE early, ints off before SCID/DWT, CTEST0 + CTEST8-CLF pulse + CTEST7 last). HIGH (CTEST0 mask literal LOW — verify empirically).

**Step 5 — Minimal SELECT + INQUIRY via SCRIPTS.** Allocate one **cache-inhibited** Fast-RAM page with the §5.5 SCRIPTS image (BE longwords, block-move bit27=0) + `IDENT(0xC0|LUN)` + `CDB(12 00 00 00 24 00)` + `DATAIN[36]` + `STATUS[1]` + `MSGIN[1]`. Patch the eight word2 phys addresses. `DSP = phys(SCRIPTS start)` to launch. On IRQ (IRQ 3): read `ISTAT(0x22)`; on `DIP` read `DSTAT(0x0F)` — `SIR(0x04)`=done (success), `BF(0x20)`=bus fault (bad phys / outside Fast RAM), `IID(0x01)`=illegal instr; on `SIP` read `SSTAT0(0x0E)` — `STO(0x20)`=no device at target, `MA(0x80)`=phase mismatch (add JUMP-WHEN-phase). Invalidate `DATAIN` cache before reading the 36 bytes. Ack via QUICKINT (shipped form: byte `0x26` to `0x408A0003`). HIGH (INT opcode low-flag bits LOW — verify the assembled `.out`).

**Step 6 — Harden** (after success): phase-dispatch JUMPs, CLEAR ATN after IDENTIFY, reselection handling, optionally switch to the A4091-firmware `DMODE=0xE0` / `CTEST8 |= SM(0x01)` values.

---

## Appendix A — Quick offset card (register base `R = 0x40800000`)
```
ISTAT  R+0x22 (RST=0x40, SIP=0x02, DIP=0x01, ABRT=0x80)   | DSTAT R+0x0F (BF=0x20, SIR=0x04, IID=0x01)
SSTAT0 R+0x0E (MA=0x80, STO=0x20, UDC=0x04, PAR=0x01)     | SIEN  R+0x00 (0xAF) | DIEN R+0x3A (0x35)
SCNTL0 R+0x03 (0xCC) | SCNTL1 R+0x02 (0x20) | SCID R+0x07 (1<<id) | SDID R+0x01 (one-hot)
CTEST8 R+0x21 (rev=V[3:0]; CLF=0x04, SM=0x01) | CTEST7 R+0x18 (CDIS=0x80)
DMODE  R+0x3B (0x80 NetBSD / 0xE0 firmware) | DCNTL R+0x38 (0x20, CF=SCLK/2) | DWT R+0x39 (0x00)
DSA R+0x10 | DSP R+0x2C (write=START) | DSPS R+0x30 (INT vector)
DBC R+0x25..27 (BE; 24-bit count) | DCMD R+0x24 | DNAD R+0x28
QUICKINT (IRQ ack) shipped: byte 0x26 -> 0x408A0003 (base form 0x40880003)   | DIP switches 0x408C0003
```

## Appendix B — Source map
| # | Domain | Key sources |
|---|---|---|
| 1 | Register map + bit defs | NCR Data Manual Jun92 Table 4-1; OpenBSD/NetBSD `osiopreg.h` (all 40 offsets reconstructed); QEMU `ncr53c710.h`; Linux `53c7xx.h`; MVME167 Table 3-10 (confirming); VxWorks `ncr710Lib` |
| 2 | SCRIPTS encoding + INQUIRY | NCR Manual Ch.5; Linux `53c700.scr`; NetBSD `oosiop.ss`/`siop.ss`; QEMU 53C710 core; A4091 fw `siop.c` (one-hot SELECT ID) |
| 3 | Init / ISR / 700-vs-710-vs-720 | NetBSD/OpenBSD `osiop.c`/`osiopreg.h`; A4091 fw `siop.c`; Linux `53c700.c`; MVME167 Table 3-10 (confirming, NOT an outlier) |
| 4 | Emulator decode + SIGSEGV | WinUAE `ncr_scsi.cpp`, `qemuvga/lsi53c710.cpp` (verified verbatim) |
| 5 | A4091 board map | a4091-software `a4091.h`/`attach.c`/`siopreg.h`; WinUAE `ncr_scsi.cpp`; Amiga Hardware Database |
| 6 | DMA / big-endian / cache / data path | NCR Manual Ch.2/5; NetBSD amiga `siop.c`/`siopreg.h`/`afsc.c`; A4091 fw `siop.c` |

---

### Verification status summary
- **§2.2 register offsets:** fully verified against `osiopreg.h` + NCR manual + MVME167 + VxWorks — **zero corrections**. MVME167 reclassified outlier→confirming.
- **§1 board map / SIGSEGV:** crash-critical `0x40800000` triple-sourced; SIGSEGV root cause verified verbatim. Fixed `A4091_ROM_SIZE` attribution (emulator, not `a4091.h`); added `A4091_IO_END=0x880000` and the shipped `0x8A0003` QUICKINT form.
- **§3 init:** corrected write order; added CTEST0 + CTEST8-CLF pulse; corrected overstated firmware literals (only DMODE `0x80`→`0xE0` and CTEST8 `SM=0x01` truly differ; SIEN/DIEN identical at `0xAF`/`0x35`). CTEST0 mask literal flagged **LOW — verify empirically**.
- **§5 SCRIPTS:** one-hot SELECT ID and block-move bit27=0 confirmed (anti-8xx-conflation); INT/RETURN low flag bits (`0x98080000` vs `0x98000000`) flagged **LOW — verify against the assembled `.out`/live chip**.