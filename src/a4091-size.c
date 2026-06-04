/*
 * a4091-size.c -- SIZE test: init-only PLUS the SCRIPTS data (inq_script[]
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
static ulong pad[240] = {
	0x00000001, 0x01010101, 0x02020203, 0x03030303, 0x04040405, 0x05050505, 0x06060607, 0x07070707, 0x08080809, 0x09090909, 0x0a0a0a0b, 0x0b0b0b0b, 0x0c0c0c0d, 0x0d0d0d0d, 0x0e0e0e0f, 0x0f0f0f0f, 0x10101011, 0x11111111, 0x12121213, 0x13131313, 0x14141415, 0x15151515, 0x16161617, 0x17171717, 0x18181819, 0x19191919, 0x1a1a1a1b, 0x1b1b1b1b, 0x1c1c1c1d, 0x1d1d1d1d, 0x1e1e1e1f, 0x1f1f1f1f, 0x20202021, 0x21212121, 0x22222223, 0x23232323, 0x24242425, 0x25252525, 0x26262627, 0x27272727, 0x28282829, 0x29292929, 0x2a2a2a2b, 0x2b2b2b2b, 0x2c2c2c2d, 0x2d2d2d2d, 0x2e2e2e2f, 0x2f2f2f2f, 0x30303031, 0x31313131, 0x32323233, 0x33333333, 0x34343435, 0x35353535, 0x36363637, 0x37373737, 0x38383839, 0x39393939, 0x3a3a3a3b, 0x3b3b3b3b, 0x3c3c3c3d, 0x3d3d3d3d, 0x3e3e3e3f, 0x3f3f3f3f, 0x40404041, 0x41414141, 0x42424243, 0x43434343, 0x44444445, 0x45454545, 0x46464647, 0x47474747, 0x48484849, 0x49494949, 0x4a4a4a4b, 0x4b4b4b4b, 0x4c4c4c4d, 0x4d4d4d4d, 0x4e4e4e4f, 0x4f4f4f4f, 0x50505051, 0x51515151, 0x52525253, 0x53535353, 0x54545455, 0x55555555, 0x56565657, 0x57575757, 0x58585859, 0x59595959, 0x5a5a5a5b, 0x5b5b5b5b, 0x5c5c5c5d, 0x5d5d5d5d, 0x5e5e5e5f, 0x5f5f5f5f, 0x60606061, 0x61616161, 0x62626263, 0x63636363, 0x64646465, 0x65656565, 0x66666667, 0x67676767, 0x68686869, 0x69696969, 0x6a6a6a6b, 0x6b6b6b6b, 0x6c6c6c6d, 0x6d6d6d6d, 0x6e6e6e6f, 0x6f6f6f6f, 0x70707071, 0x71717171, 0x72727273, 0x73737373, 0x74747475, 0x75757575, 0x76767677, 0x77777777, 0x78787879, 0x79797979, 0x7a7a7a7b, 0x7b7b7b7b, 0x7c7c7c7d, 0x7d7d7d7d, 0x7e7e7e7f, 0x7f7f7f7f, 0x80808081, 0x81818181, 0x82828283, 0x83838383, 0x84848485, 0x85858585, 0x86868687, 0x87878787, 0x88888889, 0x89898989, 0x8a8a8a8b, 0x8b8b8b8b, 0x8c8c8c8d, 0x8d8d8d8d, 0x8e8e8e8f, 0x8f8f8f8f, 0x90909091, 0x91919191, 0x92929293, 0x93939393, 0x94949495, 0x95959595, 0x96969697, 0x97979797, 0x98989899, 0x99999999, 0x9a9a9a9b, 0x9b9b9b9b, 0x9c9c9c9d, 0x9d9d9d9d, 0x9e9e9e9f, 0x9f9f9f9f, 0xa0a0a0a1, 0xa1a1a1a1, 0xa2a2a2a3, 0xa3a3a3a3, 0xa4a4a4a5, 0xa5a5a5a5, 0xa6a6a6a7, 0xa7a7a7a7, 0xa8a8a8a9, 0xa9a9a9a9, 0xaaaaaaab, 0xabababab, 0xacacacad, 0xadadadad, 0xaeaeaeaf, 0xafafafaf, 0xb0b0b0b1, 0xb1b1b1b1, 0xb2b2b2b3, 0xb3b3b3b3, 0xb4b4b4b5, 0xb5b5b5b5, 0xb6b6b6b7, 0xb7b7b7b7, 0xb8b8b8b9, 0xb9b9b9b9, 0xbabababb, 0xbbbbbbbb, 0xbcbcbcbd, 0xbdbdbdbd, 0xbebebebf, 0xbfbfbfbf, 0xc0c0c0c1, 0xc1c1c1c1, 0xc2c2c2c3, 0xc3c3c3c3, 0xc4c4c4c5, 0xc5c5c5c5, 0xc6c6c6c7, 0xc7c7c7c7, 0xc8c8c8c9, 0xc9c9c9c9, 0xcacacacb, 0xcbcbcbcb, 0xcccccccd, 0xcdcdcdcd, 0xcecececf, 0xcfcfcfcf, 0xd0d0d0d1, 0xd1d1d1d1, 0xd2d2d2d3, 0xd3d3d3d3, 0xd4d4d4d5, 0xd5d5d5d5, 0xd6d6d6d7, 0xd7d7d7d7, 0xd8d8d8d9, 0xd9d9d9d9, 0xdadadadb, 0xdbdbdbdb, 0xdcdcdcdd, 0xdddddddd, 0xdedededf, 0xdfdfdfdf, 0xe0e0e0e1, 0xe1e1e1e1, 0xe2e2e2e3, 0xe3e3e3e3, 0xe4e4e4e5, 0xe5e5e5e5, 0xe6e6e6e7, 0xe7e7e7e7, 0xe8e8e8e9, 0xe9e9e9e9, 0xeaeaeaeb, 0xebebebeb, 0xecececed, 0xedededed, 0xeeeeeeef, 0xefefefef
};

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

	if (pad[0] == 0 && pad[239] == 0) out[0] = 0;  /* keep pad linked */
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
