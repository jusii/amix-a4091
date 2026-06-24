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
static ulong inq_script[] = {
	0x47000000, 0x00000078,		/* 0x00 SELECT ATN FROM ds_Device, REL(seltimeout) */
	0x86830000, 0x00000068,		/* 0x08 JUMP REL(err_phase), WHEN NOT MSG_OUT      */
	0x1e000004, 0x00000004,		/* 0x10 MOVE FROM ds_MsgOut, WHEN MSG_OUT (IDENT)  */
	0x82830000, 0x00000058,		/* 0x18 JUMP REL(err_phase), WHEN NOT CMD          */
	0x60000008, 0x00000000,		/* 0x20 CLEAR ATN                                  */
	0x1a00000c, 0x0000000c,		/* 0x28 MOVE FROM ds_Cmd, WHEN CMD (CDB)           */
	0x81830000, 0x00000040,		/* 0x30 JUMP REL(err_phase), WHEN NOT DATA_IN      */
	0x1900003c, 0x0000003c,		/* 0x38 MOVE FROM ds_Data1, WHEN DATA_IN           */
	0x83830000, 0x00000030,		/* 0x40 JUMP REL(err_phase), WHEN NOT STATUS       */
	0x1b000014, 0x00000014,		/* 0x48 MOVE FROM ds_Status, WHEN STATUS           */
	0x87830000, 0x00000020,		/* 0x50 JUMP REL(err_phase), WHEN NOT MSG_IN       */
	0x1f00001c, 0x0000001c,		/* 0x58 MOVE FROM ds_Msg, WHEN MSG_IN (cmd-cmpl)   */
	0x60000040, 0x00000000,		/* 0x60 CLEAR ACK                                  */
	0x48000000, 0x00000000,		/* 0x68 WAIT DISCONNECT                            */
	0x98080000, 0x0000ff00,		/* 0x70 INT ok                                     */
	0x98080000, 0x0000ff05,		/* 0x78 err_phase: INT err5                        */
	0x98080000, 0x0000ff10,		/* 0x80 seltimeout: INT 0xff10                     */
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
		return ENOMEM;
	}
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
 * Issue one SCSI command (the CDB) to target via SCRIPTS; DMA the data into
 * datain_buf.  Returns: 0 done (status in status_buf[0]), -1 select timeout,
 * -2 poll timeout, -3 SCRIPTS error.  *pdsps/*pistat/*pdstat/*psstat0 get the
 * completion register state for diagnostics.
 */
static int
a4091cmd( target, cdb, cdblen, datalen, pdsps, pistat, pdstat, psstat0, ppoll)
int	target, cdblen, datalen;
uchar	*cdb;
ulong	*pdsps, *ppoll;
uchar	*pistat, *pdstat, *psstat0;
{
	ulong	poll;
	uchar	istat, dstat, sstat0;
	int	i;

	/* buffers */
	ident_buf[0] = 0x80;				/* IDENTIFY, LUN 0, no disconnect */
	for (i = 0; i < 12; ++i) cdb_buf[i] = (i < cdblen) ? cdb[i] : 0;
	for (i = 0; i < datalen && i < sizeof datain_buf; ++i) datain_buf[i] = 0;
	status_buf[0] = 0xff;
	for (i = 0; i < sizeof msg_buf; ++i) msg_buf[i] = 0xff;

	/* DSA table (bus addresses == kernel addresses, identity) */
	ds.scsi_addr = (ulong)0x10000 << target;	/* one-hot select id, async */
	ds.idlen = 1;        ds.idbuf     = ident_buf;
	ds.cmdlen = cdblen;  ds.cmdbuf    = cdb_buf;
	ds.stslen = 1;       ds.stsbuf    = status_buf;
	ds.msglen = 1;       ds.msgbuf    = &msg_buf[0];
	ds.msginlen = 1;     ds.msginbuf  = &msg_buf[1];
	ds.extmsglen = 1;    ds.extmsgbuf = &msg_buf[2];
	ds.synmsglen = 1;    ds.synmsgbuf = &msg_buf[3];
	ds.data1len = datalen; ds.data1buf = datain_buf;
	ds.data2len = 0;     ds.data2buf  = 0;

	/* launch (siop.c:1108-1111) */
	WR32( R_TEMP, 0);
	WR8 ( R_SBCL, 0);				/* async */
	WR32( R_DSA, (ulong)&ds);
	WR32( R_DSP, (ulong)inq_script);		/* writing DSP starts SCRIPTS */

	/* poll ISTAT for SIP|DIP (siop_poll, siop.c:357) */
	istat = 0;
	for (poll = 0; poll < 8000000; ++poll) {
		istat = RD8(R_ISTAT);
		if (istat & (ISTAT_SIP | ISTAT_DIP))
			break;
	}
	sstat0 = (istat & ISTAT_SIP) ? RD8(R_SSTAT0) : 0;
	dstat  = (istat & ISTAT_DIP) ? RD8(R_DSTAT)  : 0;
	*pdsps = RD32(R_DSPS);
	*pistat = istat; *pdstat = dstat; *psstat0 = sstat0; *ppoll = poll;

	if (!(istat & (ISTAT_SIP | ISTAT_DIP)))
		return -2;				/* poll timeout (chip silent) */
	if (sstat0 & SSTAT0_STO)
		return -1;				/* select timeout (no target) */
	if ((dstat & DSTAT_SIR) && *pdsps == 0xff00)
		return 0;				/* command complete */
	return -3;					/* SCRIPTS error (dsps=code) */
}

/*
 * GSIO entry.  Run cp->cdb as a SCSI command to target cp->unit and fill cp->addr
 * with: [0..35] INQUIRY data, then a diagnostics block at [36..].
 */
bool
a4091queue( c, cp)
int		c;
struct sdcom	*cp;
{
	uchar	*out = (uchar *)cp->addr;
	ulong	dsps, poll, scr;
	uchar	istat, dstat, sstat0;
	int	e, i, rc, dlen;

	if (e = a4091map()) {
		cp->status = 0xff; cp->okay = FALSE;
		(*cp->intr)( cp);
		return TRUE;
	}

	/* write-path self-test: SCRATCH via shadow, read back at +0 */
	WR32( R_SCRATCH, 0x5aa55aa5);
	scr = RD32( R_SCRATCH);

	a4091init();

	dlen = cp->nbyte ? cp->nbyte : 36;
	if (dlen > sizeof datain_buf) dlen = sizeof datain_buf;
	rc = a4091cmd( (int)cp->unit, cp->cdb, 6, dlen,
		       &dsps, &istat, &dstat, &sstat0, &poll);

	/* [0..35] the data the target sent (INQUIRY response) */
	for (i = 0; i < 36; ++i)
		out[i] = datain_buf[i];
	/* [36..] diagnostics */
	out[36] = (uchar)rc;			/* 0 done, -1 STO, -2 pollto, -3 err */
	out[37] = status_buf[0];		/* SCSI status (0=GOOD) */
	out[38] = istat;
	out[39] = dstat;
	out[40] = sstat0;
	out[41] = (RD8(R_CTEST8) >> 4) & 0xf;	/* chip rev */
	out[42] = (scr == 0x5aa55aa5) ? 0xab : 0x00;	/* shadow-write OK? */
	out[43] = RD8(R_SCNTL0);		/* echo a reg we set (0xCC if write OK) */
	out[44] = (uchar)(dsps >> 24); out[45] = (uchar)(dsps >> 16);
	out[46] = (uchar)(dsps >> 8);  out[47] = (uchar)dsps;
	out[48] = (uchar)(poll >> 24); out[49] = (uchar)(poll >> 16);
	out[50] = (uchar)(poll >> 8);  out[51] = (uchar)poll;
	out[52] = msg_buf[0];			/* command-complete message (0x00) */

	cp->nbyte = 53;
	cp->status = (rc == 0) ? status_buf[0] : 0xff;
	cp->okay = (rc == 0) ? TRUE : FALSE;
	(*cp->intr)( cp);
	return TRUE;
}

/* INT2 ISR placeholder; not wired (we poll). */
void
a4091intr()
{
}
