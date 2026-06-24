/* SPDX-License-Identifier: MIT */
/*
 * a4091-data.c -- bisection step 2: init-only PLUS the SCRIPTS data (inq_script[]
 * .data array, siop_ds struct, buffers) but NO a4091cmd transaction code.
 * a4091queue references the data so it is linked.  If this boots, the boot-breaker
 * is the a4091cmd CODE; if it Gurus, it is the data (inq_script .data initializer).
 */
#include	"sys/types.h"
#include	"sys/immu.h"
#include	"sys/errno.h"
#include	"rico.h"
#include	"sd.h"

#define	A4091_PROD	0x02020054
#define	SIOP_OFF	0x00800000
#define	WRSHADOW	0x40

#define	R_SCNTL0	0x03
#define	R_SCID		0x07
#define	R_ISTAT		0x22
#define	R_SCRATCH	0x34
#define	R_DMODE		0x3B
#define	R_CTEST8	0x21

extern int	autocon();
extern caddr_t	sptalloc();

static volatile uchar	*acfg;
static volatile uchar	*siop;
static long		board_phys;

#define	RD8(r)		(siop[(r)])
#define	WR8(r,v)	(siop[(r)+WRSHADOW] = (uchar)(v))
#define	RD32(r)		(*(volatile ulong *)(siop + (r)))
#define	WR32(r,v)	(*(volatile ulong *)(siop + (r) + WRSHADOW) = (ulong)(v))

/* THE SUSPECT: a .data initialized array (init-only/detection have 0 .data) */
static ulong inq_script[] = {
	0x47000000, 0x00000078, 0x86830000, 0x00000068,
	0x1e000004, 0x00000004, 0x82830000, 0x00000058,
	0x60000008, 0x00000000, 0x1a00000c, 0x0000000c,
	0x81830000, 0x00000040, 0x1900003c, 0x0000003c,
	0x83830000, 0x00000030, 0x1b000014, 0x00000014,
	0x87830000, 0x00000020, 0x1f00001c, 0x0000001c,
	0x60000040, 0x00000000, 0x48000000, 0x00000000,
	0x98080000, 0x0000ff00, 0x98080000, 0x0000ff05,
	0x98080000, 0x0000ff10,
};

static struct siop_ds {
	ulong	scsi_addr;
	ulong	idlen;    uchar	*idbuf;
	ulong	cmdlen;   uchar	*cmdbuf;
	ulong	stslen;   uchar	*stsbuf;
	ulong	msglen;   uchar	*msgbuf;
	ulong	msginlen; uchar	*msginbuf;
	ulong	extmsglen;uchar	*extmsgbuf;
	ulong	synmsglen;uchar	*synmsgbuf;
	ulong	data1len; uchar	*data1buf;
	ulong	data2len; uchar	*data2buf;
} ds;

static uchar	ident_buf[4];
static uchar	cdb_buf[12];
static uchar	datain_buf[36];
static uchar	status_buf[4];
static uchar	msg_buf[8];

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

	for (i = 0; i < 0x10; ++i) out[i] = acfg[i];
	for (i = 0; i < 0x40; ++i) out[0x10 + i] = siop[i];

	WR32( R_SCRATCH, 0x5aa55aa5);
	scr = RD32( R_SCRATCH);

	/* reference the data so it is linked (touch, not transact) */
	ds.scsi_addr = 0x00010000;
	ds.idbuf = ident_buf; ds.cmdbuf = cdb_buf; ds.data1buf = datain_buf;
	ds.stsbuf = status_buf; ds.msgbuf = msg_buf;
	ident_buf[0] = 0x80; cdb_buf[0] = cp->cdb[0]; datain_buf[0] = 0;

	out[0x50] = (scr == 0x5aa55aa5) ? 0xab : 0x00;
	out[0x51] = RD8( R_SCNTL0);
	out[0x52] = (RD8( R_CTEST8) >> 4) & 0xf;
	out[0x53] = (uchar)inq_script[0];	/* link inq_script: 0x47 */
	out[0x54] = (uchar)inq_script[33];	/* link tail: 0x10 (from 0xff10) */
	out[0x55] = (uchar)(ds.scsi_addr >> 16);	/* link ds: 0x01 */
	out[0x56] = ds.idbuf[0];		/* link buffers: 0x80 */

	cp->nbyte = 0x57;
	cp->status = 0;
	cp->okay = TRUE;
	(*cp->intr)( cp);
	return TRUE;
}

void
a4091intr()
{
}
