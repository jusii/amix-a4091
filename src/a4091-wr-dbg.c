/* SPDX-License-Identifier: MIT */
/*
 * a4091.c -- Commodore A4091 (Zorro III / NCR 53C710) SCSI host adapter for Amix.
 *
 * PHASE 2: real SCSI.  Detection (phase 1) proved the kernel can sptalloc-map the
 * Zorro III window (board @ phys 0x40000000, inside the 68030 TT GAP) and read the
 * 53C710 registers (CTEST8 high nibble = chip rev).  This phase drives the chip:
 * soft-reset + init, then a minimal SELECT + INQUIRY to a target via the 53C710
 * SCRIPTS engine, returning the 36-byte INQUIRY response (the first proof Amix can
 * talk to the drive *behind* the A4091).
 *
 * a4091queue() is reached via the GSIO raw-SCSI passthrough (/dev/scsi).  The caller
 * supplies cp->cdb (the CDB, e.g. INQUIRY 12 00 00 00 24 00), cp->unit (target id),
 * cp->addr (result buffer).  We run the transaction and fill cp->addr with the
 * INQUIRY data followed by diagnostics.
 *
 * 53C710 facts for the A4091 (cross-checked vs A4091/a4091-software siop.c):
 *   - register file at board+0x800000; READS at reg+0x00, WRITES at reg+0x40 (shadow).
 *   - 8/32-bit accesses only, never 16-bit.  big-endian byte lanes.
 *   - soft reset = ISTAT bit6 (0x40), NOT self-clearing.
 *   - SCID is a one-hot id bitmask (1<<7 = 0x80 for host id 7).
 *   - DMODE=0xE0 (burst8|FC2, A4091), DCNTL=0x00 (Zorro, CF=SCLK/2 @50MHz).
 *   - CTEST8 |= SM(0x01) bus-arbitration errata workaround (A4091-specific).
 * DMA model on Amix: kvtop()==identity (kernel VA==PA via 68030 TT0), data cache
 * off (Amiberry cpu_data_cache=false), so a static array's address is the bus
 * address the chip DMAs to -- no translation, no cache flush (matches a2091.c).
 *
 * Refs: reference/a4091-study.md (init recipe, SCRIPTS, DSA layout, done-detection),
 *       reference/a4091-software/siop.c + siop_script.ss (53C710 / ARCH_710).
 */
#include	"sys/types.h"
#include	"sys/immu.h"		/* PG_V, phystopfn, paddr_t */
#include	"sys/errno.h"
#include	"rico.h"
#include	"sd.h"

#define	A4091_PROD	0x02020054	/* AutoConfig: Commodore(0x0202) product 0x54 */
#define	SIOP_OFF	0x00800000	/* 53C710 register block within the board window */
#define	WRSHADOW	0x40		/* A4091 write-shadow: writes go to reg+0x40 */

/* 53C710 register offsets (native, ARCH_710; read at +0, write at +WRSHADOW) */
#define	R_SIEN		0x00
#define	R_SCNTL1	0x02
#define	R_SCNTL0	0x03
#define	R_SCID		0x07
#define	R_SBCL		0x08
#define	R_SSTAT0	0x0E
#define	R_DSTAT		0x0F
#define	R_DSA		0x10		/* 32-bit */
#define	R_CTEST0	0x17
#define	R_CTEST7	0x18
#define	R_TEMP		0x1C		/* 32-bit */
#define	R_CTEST8	0x21		/* high nibble = chip rev */
#define	R_ISTAT		0x22
#define	R_DSP		0x2C		/* 32-bit; writing it starts SCRIPTS */
#define	R_DSPS		0x30		/* 32-bit; INT code lands here */
#define	R_SCRATCH	0x34		/* 32-bit scratch (for write self-test) */
#define	R_DCNTL		0x38
#define	R_DWT		0x39
#define	R_DIEN		0x3A
#define	R_DMODE		0x3B

/* register bits */
#define	ISTAT_ABRT	0x80
#define	ISTAT_RST	0x40
#define	ISTAT_SIP	0x02
#define	ISTAT_DIP	0x01
#define	DSTAT_SIR	0x04		/* SCRIPTS INT instruction */
#define	SSTAT0_STO	0x20		/* selection timeout */

#define	HOST_ID		7

extern int	autocon();
extern caddr_t	sptalloc();
extern int	printf();

static volatile uchar	*acfg;		/* board base page (AutoConfig regs) */
static volatile uchar	*siop;		/* 53C710 register block (read base) */
static long		board_phys;

/* register accessors -- reads at +0, writes at +WRSHADOW; 8/32-bit only */
#define	RD8(r)		(siop[(r)])
#define	WR8(r,v)	(siop[(r)+WRSHADOW] = (uchar)(v))
#define	RD32(r)		(*(volatile ulong *)(siop + (r)))
#define	WR32(r,v)	(*(volatile ulong *)(siop + (r) + WRSHADOW) = (ulong)(v))

/*
 * The minimal table-indirect INQUIRY SCRIPTS (assembled by ncr53cxxx from
 * reference/scripts/inq.ss; 17 instructions / 136 bytes).  DSPS result vectors:
 * ok=0xff00, err5(unexpected phase)=0xff05, seltimeout=0xff10.  Static array =>
 * lives in kernel .data => bus address == &inq_script[0] (TT0 identity).
 */
static ulong inq_script[] = {		/* READ+WRITE: DATA phase dispatches on live bus phase */
	0x47000000, 0x000000a0,		/* SELECT ATN FROM ds_Device, REL(seltimeout)   */
	0x86830000, 0x00000090,		/* JUMP err_phase, WHEN NOT MSG_OUT             */
	0x1e000004, 0x00000004,		/* MOVE FROM ds_MsgOut, WHEN MSG_OUT (IDENTIFY) */
	0x82830000, 0x00000080,		/* JUMP err_phase, WHEN NOT CMD                 */
	0x60000008, 0x00000000,		/* CLEAR ATN                                    */
	0x1a00000c, 0x0000000c,		/* MOVE FROM ds_Cmd, WHEN CMD                   */
	0x808b0000, 0x00000018,		/* JUMP dataout, WHEN DATA_OUT  (write)         */
	0x818b0000, 0x00000020,		/* JUMP datain,  WHEN DATA_IN   (read)          */
	0x838b0000, 0x00000020,		/* JUMP status,  WHEN STATUS    (no data)       */
	0x80880000, 0x00000050,		/* JUMP err_phase               (unexpected)    */
	0x1800003c, 0x0000003c,		/* dataout: MOVE FROM ds_Data1, WHEN DATA_OUT   */
	0x80880000, 0x00000008,		/* JUMP status_phase                            */
	0x1900003c, 0x0000003c,		/* datain:  MOVE FROM ds_Data1, WHEN DATA_IN    */
	0x83830000, 0x00000030,		/* status_phase: JUMP err_phase, WHEN NOT STATUS*/
	0x1b000014, 0x00000014,		/* MOVE FROM ds_Status, WHEN STATUS             */
	0x87830000, 0x00000020,		/* JUMP err_phase, WHEN NOT MSG_IN              */
	0x1f00001c, 0x0000001c,		/* MOVE FROM ds_Msg, WHEN MSG_IN                */
	0x60000040, 0x00000000,		/* CLEAR ACK                                    */
	0x48000000, 0x00000000,		/* WAIT DISCONNECT                              */
	0x98080000, 0x0000ff00,		/* INT ok                                       */
	0x98080000, 0x0000ff05,		/* err_phase: INT err5                          */
	0x98080000, 0x0000ff10,		/* seltimeout: INT 0xff10                       */
};

/*
 * The data structure the SCRIPTS read via DSA (mirror of siop_ds, siopvar.h:56-76).
 * Each {len,ptr} pair is 8 bytes; scsi_addr is a lone selector word at offset 0.
 * All longwords big-endian, pointers are bus addresses (identity).
 */
static struct siop_ds {
	ulong	scsi_addr;		/* 0x00  (1<<(16+target)) | (sxfer<<8) */
	ulong	idlen;    uchar	*idbuf;		/* 0x04 ds_MsgOut (IDENTIFY) */
	ulong	cmdlen;   uchar	*cmdbuf;	/* 0x0c ds_Cmd     (CDB)     */
	ulong	stslen;   uchar	*stsbuf;	/* 0x14 ds_Status            */
	ulong	msglen;   uchar	*msgbuf;	/* 0x1c ds_Msg     (cmd-cmpl)*/
	ulong	msginlen; uchar	*msginbuf;	/* 0x24 ds_MsgIn             */
	ulong	extmsglen;uchar	*extmsgbuf;	/* 0x2c ds_ExtMsg            */
	ulong	synmsglen;uchar	*synmsgbuf;	/* 0x34 ds_SyncMsg           */
	ulong	data1len; uchar	*data1buf;	/* 0x3c ds_Data1  (INQUIRY)  */
	ulong	data2len; uchar	*data2buf;	/* 0x44 ds_Data2  (chain end)*/
} ds;

static uchar	ident_buf[4];
static uchar	cdb_buf[12];
static uchar	datain_buf[36];
static uchar	status_buf[4];
static uchar	msg_buf[8];

/* last-transaction diagnostics (data path goes straight to cp->addr now) */
ulong	a4091_dsps;
uchar	a4091_rc, a4091_istat, a4091_dstat, a4091_stat;

/*
 * Map the two safe A4091 sub-regions into kernel VA.  0 on success, errno else.
 */
static int
a4091map()
{
	long	base, size;

	if (siop)
		return 0;
	unless (autocon( A4091_PROD, 0, &base, &size)) {
		base = 0x40000000;		/* known A4091 Zorro III phys base */
		size = 0x01000000;
	}
	board_phys = base;
	acfg = (volatile uchar *)sptalloc( 1, PG_V, phystopfn( (paddr_t)base), 0);
	siop = (volatile uchar *)sptalloc( 1, PG_V, phystopfn( (paddr_t)base + SIOP_OFF), 0);
	if (acfg == 0 || siop == 0) {
		siop = 0;
		printf("A4:MAP FAIL acfg=%x siop=%x\n", (uint)acfg, (uint)siop);
		return ENOMEM;
	}
	printf("A4:MAP ok base=%x siop=%x\n", (uint)board_phys, (uint)siop);
	return 0;
}

/*
 * 53C710 soft reset + minimal polled-mode init (subset of siopreset, siop.c:800-826).
 * No SCSI/DMA interrupts (we poll).  No bus reset / 250ms settle for the emulated
 * target -- add them if SELECT times out on real hardware.
 */
static void
a4091init()
{
	WR8( R_ISTAT,  RD8(R_ISTAT) | ISTAT_ABRT);	/* abort any running script */
	WR8( R_ISTAT,  RD8(R_ISTAT) | ISTAT_RST);	/* soft reset...            */
	WR8( R_ISTAT,  RD8(R_ISTAT) & ~ISTAT_RST);	/* ...clear (not self)      */
	WR8( R_SIEN,   0x00);
	WR8( R_SCNTL0, 0xCC);				/* ARB_FULL|EPC|EPG         */
	WR8( R_SCNTL1, 0x20);				/* ESR enable sel/resel     */
	WR8( R_DCNTL,  0x00);				/* CF=SCLK/2 @50MHz, no EA   */
	WR8( R_DMODE,  0xE0);				/* burst8|FC2 (A4091)       */
	WR8( R_SIEN,   0x00);
	WR8( R_DIEN,   0x00);
	WR8( R_SCID,   1 << HOST_ID);			/* one-hot host id (0x80)   */
	WR8( R_DWT,    0x00);
	WR8( R_CTEST0, RD8(R_CTEST0) | 0x50);		/* BTD|EAN                  */
	WR8( R_CTEST7, RD8(R_CTEST7) | 0x80);		/* CDIS                     */
	WR8( R_CTEST8, RD8(R_CTEST8) | 0x01);		/* SM errata (A4091)        */
	{ uchar i = RD8(R_ISTAT);			/* clear stale latched ints */
	  if (i & ISTAT_SIP) (void)RD8(R_SSTAT0);
	  if (i & ISTAT_DIP) (void)RD8(R_DSTAT); }
}

/*
 * Generic SCSI queue entry.  Drives ANY CDB from the caller's sdcom (cp->cdb,
 * cp->nbyte, cp->addr) through the 53C710 SCRIPTS -- mirrors a3091queue.  CDB
 * length is derived from the opcode group code; data DMAs straight into cp->addr
 * (identity VA==PA).  READ path (DATA_IN) only; writes (DATA_OUT) need a SCRIPTS
 * branch (deferred).  Still 'nopoll' (single ISTAT read): the emulated target
 * answers instantly, and a poll loop currently breaks the kernel boot (NOTES S12).
 */
bool
a4091queue( c, cp)
int		c;
struct sdcom	*cp;
{
	ulong	dsps;
	uchar	istat, dstat, sstat0;
	int	e, i, rc, target, cmdlen;

	if (e = a4091map()) {
		printf("A4:Q mapfail e=%d\n", e);
		cp->status = 0xff; cp->okay = FALSE;
		(*cp->intr)( cp);
		return TRUE;
	}

	a4091init();

	target = (int)cp->unit;

	/* CDB length from SCSI group code (top 3 bits of opcode): 0->6, 5->12, else 10 */
	cmdlen = ((cp->cdb[0] >> 5) & 7) == 0 ? 6
	       : ((cp->cdb[0] >> 5) & 7) == 5 ? 12 : 10;

	ident_buf[0] = 0x80;				/* IDENTIFY, no disconnect */
	for (i = 0; i < 12; ++i) cdb_buf[i] = cp->cdb[i];
	status_buf[0] = 0xff;
	for (i = 0; i < 8; ++i) msg_buf[i] = 0xff;

	/* DSA table -- command + data bound to the caller's sdcom */
	ds.scsi_addr = (ulong)0x10000 << target;
	ds.idlen = 1;        ds.idbuf     = ident_buf;
	ds.cmdlen = cmdlen;  ds.cmdbuf    = cdb_buf;
	ds.stslen = 1;       ds.stsbuf    = status_buf;
	ds.msglen = 1;       ds.msgbuf    = &msg_buf[0];
	ds.msginlen = 1;     ds.msginbuf  = &msg_buf[1];
	ds.extmsglen = 1;    ds.extmsgbuf = &msg_buf[2];
	ds.synmsglen = 1;    ds.synmsgbuf = &msg_buf[3];
	ds.data1len = cp->nbyte;  ds.data1buf = (uchar *)cp->addr;	/* DMA -> caller buf */
	ds.data2len = 0;     ds.data2buf  = 0;

	/* launch */
	WR32( R_TEMP, 0);
	WR8 ( R_SBCL, 0);
	WR32( R_DSA, (ulong)&ds);
	WR32( R_DSP, (ulong)inq_script);

	/* nopoll completion -- single ISTAT read (emulated target instant) */
	istat = RD8(R_ISTAT);
	sstat0 = (istat & ISTAT_SIP) ? RD8(R_SSTAT0) : 0;
	dstat  = (istat & ISTAT_DIP) ? RD8(R_DSTAT)  : 0;
	dsps   = RD32(R_DSPS);

	if (!(istat & (ISTAT_SIP | ISTAT_DIP)))         rc = -2;
	else if (sstat0 & SSTAT0_STO)                   rc = -1;
	else if ((dstat & DSTAT_SIR) && dsps == 0xff00) rc = 0;
	else                                            rc = -3;

	/* stash diagnostics (data already DMA'd into cp->addr) */
	a4091_rc = (uchar)rc;  a4091_dsps = dsps;
	a4091_istat = istat;   a4091_dstat = dstat;  a4091_stat = status_buf[0];

	printf("A4:Q u%d op%x n%d rc%d ds%x st%x\n",
		target, (cp->cdb[0])&0xff, cp->nbyte, rc, (uint)dsps, (status_buf[0])&0xff);
	cp->status = (rc == 0) ? status_buf[0] : 0xff;
	cp->okay   = (rc == 0) ? TRUE : FALSE;
	(*cp->intr)( cp);
	return TRUE;
}

void
a4091intr()
{
}
