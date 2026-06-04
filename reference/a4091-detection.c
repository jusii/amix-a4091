/*
 * a4091.c -- Commodore A4091 (Zorro III / NCR 53C710) SCSI host adapter for Amix.
 *
 * DETECTION MILESTONE.  Proves two things end to end from the Amix KERNEL:
 *   1. sptalloc() can map the A4091's Zorro III window into kernel VA, even
 *      though the board sits at physical 0x40000000 -- inside the 68030
 *      transparent-translation GAP (0x40000000-0x7FFFFFFF, amiga/ml/ttrap.s)
 *      that the stock kernel cannot reach.  (autocon(0x02020054) gives the base;
 *      the stock Zorro II drivers never needed sptalloc because their boards are
 *      all in the TT0 identity range.)
 *   2. The NCR 53C710 register file at board+0x00800000 (= 0x40800000) responds
 *      with correct byte lanes -- CTEST8 (A4091 BE offset 0x21) high nibble is
 *      the chip revision.
 *
 * a4091queue() returns, as the data of a GSIO request, the AutoConfig nibble
 * regs (board base) followed by the 53C710 register file, so userspace
 * `gsio 1 0` can read them.  Only the two known-safe sub-regions are mapped
 * (base page + register page); the rest of the 16 MB window is NOT touched
 * (a full-window read SIGSEGVs the Amiberry emulation).
 *
 * Refs: docs/a4091-53c710-reference.md (board map, register map, bring-up plan).
 * Build: place in amiga/alien/, add a4091.o to OBJ in amiga/alien/Makefile,
 * add { 0x02020054, &a4091queue, "A4091 SCSI" } to scsicard[] in sd.c, relink.
 */
#include	"sys/types.h"
#include	"sys/immu.h"		/* PG_V, phystopfn, paddr_t */
#include	"sys/errno.h"
#include	"rico.h"
#include	"sd.h"

#define	A4091_PROD	0x02020054	/* AutoConfig: Commodore(0x0202) product 0x54 */
#define	SIOP_OFF	0x00800000	/* 53C710 register block, offset within board window */

/* 53C710 register offsets, A4091 big-endian byte lanes (see reference table 2.2) */
#define	R_SIEN		0x00
#define	R_SCNTL1	0x02
#define	R_SCNTL0	0x03
#define	R_SCID		0x07
#define	R_DSTAT		0x0F
#define	R_CTEST8	0x21		/* high nibble = chip revision (read-only) */
#define	R_ISTAT		0x22
#define	R_DCNTL		0x38
#define	R_DMODE		0x3B

extern int	autocon();
extern caddr_t	sptalloc();
extern int	printf();

static volatile uchar	*acfg;		/* board base page (AutoConfig nibble regs) */
static volatile uchar	*siop;		/* 53C710 register block */
static long		board_phys;

/*
 * Map the two safe A4091 sub-regions into kernel VA.  0 on success, errno else.
 * Idempotent.
 */
static int
a4091map()
{
	long	base, size;

	if (siop)
		return 0;
	unless (autocon( A4091_PROD, 0, &base, &size)) {
		/* autocon didn't surface the board (its bootinfo match is failing for
		 * the Zorro III A4091).  Fall back to the known A4091 physical base so
		 * the detection test can still exercise the sptalloc + 53C710 path. */
		base = 0x40000000;
		size = 0x01000000;
		printf( "a4091: autocon miss, using fixed base 0x%x\n", base);
	}
	board_phys = base;

	/* page at board base: AutoConfig nibble registers (safe, <256KB ROM buffer) */
	acfg = (volatile uchar *)sptalloc( 1, PG_V, phystopfn( (paddr_t)base), 0);
	/* page at board+0x800000: the 53C710 register file (safe I/O decode) */
	siop = (volatile uchar *)sptalloc( 1, PG_V, phystopfn( (paddr_t)base + SIOP_OFF), 0);
	if (acfg == 0 || siop == 0) {
		siop = 0;
		return ENOMEM;			/* sptalloc refused the device page? */
	}

	printf( "a4091: base 0x%x  acfg=0x%x  siop=0x%x\n", base, acfg, siop);
	printf( "a4091: autoconfig %x %x %x %x\n",
		acfg[0x00], acfg[0x04], acfg[0x08], acfg[0x0c]);
	printf( "a4091: 53c710 CTEST8=0x%x rev=%d  ISTAT=0x%x SCNTL0=0x%x SCID=0x%x DSTAT=0x%x\n",
		siop[R_CTEST8], (siop[R_CTEST8] >> 4) & 0xf,
		siop[R_ISTAT], siop[R_SCNTL0], siop[R_SCID], siop[R_DSTAT]);
	return 0;
}

/*
 * GSIO entry point.  Maps the board (first call) and returns, as the request
 * data, 16 bytes of AutoConfig regs followed by the 64-byte 53C710 register
 * file.  Completes synchronously (no SCSI yet).
 */
bool
a4091queue( c, cp)
int		c;
struct sdcom	*cp;
{
	uchar	*out = (uchar *)cp->addr;
	int	i, e;

	if (e = a4091map()) {
		cp->status = 0xff;
		cp->okay = FALSE;
		(*cp->intr)( cp);
		return TRUE;
	}
	for (i = 0; i < 0x10; ++i)
		out[i] = acfg[i];			/* AutoConfig nibble regs */
	for (i = 0; i < 0x40; ++i)
		out[0x10 + i] = siop[i];		/* 53C710 register file */
	cp->nbyte = 0x50;
	cp->status = 0;
	cp->okay = TRUE;
	(*cp->intr)( cp);
	return TRUE;
}

/* INT2 ISR placeholder; wired into int2_tbl[] once we drive the chip for real. */
void
a4091intr()
{
}
