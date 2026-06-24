/* SPDX-License-Identifier: MIT */
/* scscan.c -- find the live sd.c scsicard[] and queue[] in kernel memory by
 * content (robust against symbol address drift). Prints the rows.
 * Usage: scscan [hexstart hexend]
 */
#include <sys/types.h>
#include <stdio.h>
#include <fcntl.h>

static int fd;

static unsigned long rdl(a)
unsigned long a;
{
    unsigned char b[4];
    lseek(fd, (long)a, 0);
    if (read(fd, (char *)b, 4) != 4) return 0xffffffffUL;
    return ((unsigned long)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
}

int main(argc, argv)
int argc;
char **argv;
{
    unsigned long start = 0x07800000, end = 0x07a00000, a;
    unsigned long buf[256];

    if (argc > 2) { start = strtoul(argv[1], 0, 16); end = strtoul(argv[2], 0, 16); }
    if ((fd = open("/dev/kmem", O_RDONLY)) < 0) { perror("/dev/kmem"); return 1; }

    for (a = start; a < end; a += sizeof buf) {
        int got, i;
        lseek(fd, (long)a, 0);
        got = read(fd, (char *)buf, sizeof buf);
        if (got <= 0) continue;
        for (i = 0; i + 4 <= got; i += 4) {
            unsigned long v = buf[i / 4];
            unsigned long at = a + i;
            if (v == 0x0202f003UL) {
                /* candidate scsicard[0].pn -- check it's the table */
                unsigned long p1 = rdl(at + 12), p2 = rdl(at + 24), p3 = rdl(at + 36);
                printf("scsicard @0x%08lx: pn[0..3]= %08lx %08lx %08lx %08lx %s\n",
                       at, v, p1, p2, p3,
                       (p3 == 0x02020054UL) ? "<== 4-ROW (A4091 present)" :
                       (p1 == 0x02020001UL) ? "<== 3-ROW (no A4091)" : "");
                if (p1 == 0x02020001UL) {
                    int r;
                    for (r = 0; r < 4; r++)
                        printf("   row %d: pn=0x%08lx f=0x%08lx name=0x%08lx\n", r,
                               rdl(at + r*12), rdl(at + r*12 + 4), rdl(at + r*12 + 8));
                }
            }
            if (v == 0x00dd0000UL) {
                /* candidate queue[0].a (board addr); queue[0] starts 8 bytes back */
                unsigned long q = at - 8;
                printf("queue @0x%08lx: q0{f=0x%08lx c=0x%08lx a=0x%08lx} q1{f=0x%08lx c=0x%08lx a=0x%08lx}\n",
                       q, rdl(q), rdl(q+4), rdl(q+8), rdl(q+16), rdl(q+20), rdl(q+24));
            }
        }
    }
    return 0;
}
