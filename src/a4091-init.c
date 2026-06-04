/*
 * a4091-init.c -- incremental step toward real SCSI: detection + 53C710
 * soft-reset/init + register write-path self-test.  NO SCRIPTS yet.
 * Used to bisect why the full real-SCSI driver's kernel image won't boot
 * (the driver code never runs at boot, so a boot failure = a build/codegen
 * issue, not execution).  This version omits the large inq_script[] array,
 * the siop_ds struct, and the a4091cmd transaction function.
 */
#include	"sys/types.h"
#include	"sys/immu.h"
#include	"sys/errno.h"
#include	"rico.h"
#include	"sd.h"

#define	A4091_PROD	0x02020054
#define	SIOP_OFF	0x00800000
#define	WRSHADOW	0x40

#define	R_SIEN		0x00
#define	R_SCNTL1	0x02
#define	R_SCNTL0	0x03
#define	R_SCID		0x07
#define	R_SSTAT0	0x0E
#define	R_DSTAT		0x0F
#define	R_CTEST0	0x17
#define	R_CTEST7	0x18
#define	R_CTEST8	0x21
#define	R_ISTAT		0x22
#define	R_SCRATCH	0x34
#define	R_DCNTL		0x38
#define	R_DWT		0x39
#define	R_DIEN		0x3A
#define	R_DMODE		0x3B

#define	ISTAT_ABRT	0x80
#define	ISTAT_RST	0x40
#define	ISTAT_SIP	0x02
#define	ISTAT_DIP	0x01
#define	HOST_ID		7

extern int	autocon();
extern caddr_t	sptalloc();

static volatile uchar	*acfg;
static volatile uchar	*siop;
static long		board_phys;

#define	RD8(r)		(siop[(r)])
#define	WR8(r,v)	(siop[(r)+WRSHADOW] = (uchar)(v))
#define	RD32(r)		(*(volatile ulong *)(siop + (r)))
#define	WR32(r,v)	(*(volatile ulong *)(siop + (r) + WRSHADOW) = (ulong)(v))

static int
a4091map()
{
	long	base, size;

	if (siop)
		return 0;
	unless (autocon( A4091_PROD, 0, &base, &size)) {
		base = 0x40000000;
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

static void
a4091init()
{
	WR8( R_ISTAT,  RD8(R_ISTAT) | ISTAT_ABRT);
	WR8( R_ISTAT,  RD8(R_ISTAT) | ISTAT_RST);
	WR8( R_ISTAT,  RD8(R_ISTAT) & ~ISTAT_RST);
	WR8( R_SIEN,   0x00);
	WR8( R_SCNTL0, 0xCC);
	WR8( R_SCNTL1, 0x20);
	WR8( R_DCNTL,  0x00);
	WR8( R_DMODE,  0xE0);
	WR8( R_SIEN,   0x00);
	WR8( R_DIEN,   0x00);
	WR8( R_SCID,   1 << HOST_ID);
	WR8( R_DWT,    0x00);
	WR8( R_CTEST0, RD8(R_CTEST0) | 0x50);
	WR8( R_CTEST7, RD8(R_CTEST7) | 0x80);
	WR8( R_CTEST8, RD8(R_CTEST8) | 0x01);
	{ uchar i = RD8(R_ISTAT);
	  if (i & ISTAT_SIP) (void)RD8(R_SSTAT0);
	  if (i & ISTAT_DIP) (void)RD8(R_DSTAT); }
}

bool
a4091queue( c, cp)
int		c;
struct sdcom	*cp;
{
	uchar	*out = (uchar *)cp->addr;
	ulong	scr;
	int	e, i;

	if (e = a4091map()) {
		cp->status = 0xff; cp->okay = FALSE;
		(*cp->intr)( cp);
		return TRUE;
	}

	/* pre-init: AutoConfig regs + 53C710 register file (like detection) */
	for (i = 0; i < 0x10; ++i)
		out[i] = acfg[i];
	for (i = 0; i < 0x40; ++i)
		out[0x10 + i] = siop[i];

	/* write-path self-test: 32-bit SCRATCH via shadow, read back at +0 */
	WR32( R_SCRATCH, 0x5aa55aa5);
	scr = RD32( R_SCRATCH);

	a4091init();

	/* post-init diagnostics */
	out[0x50] = (scr == 0x5aa55aa5) ? 0xab : 0x00;	/* 32-bit shadow write OK? */
	out[0x51] = RD8( R_SCNTL0);			/* 0xCC if 8-bit write OK   */
	out[0x52] = RD8( R_SCID);			/* 0x80 if write OK         */
	out[0x53] = RD8( R_DMODE);			/* 0xE0                     */
	out[0x54] = (RD8( R_CTEST8) >> 4) & 0xf;	/* chip rev                 */
	out[0x55] = RD8( R_ISTAT);

	cp->nbyte = 0x56;
	cp->status = 0;
	cp->okay = TRUE;
	(*cp->intr)( cp);
	return TRUE;
}

void
a4091intr()
{
}
