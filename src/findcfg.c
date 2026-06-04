/* findcfg.c -- scan /dev/kmem for the bootinfo ConfigDev table by locating the
 * known board base addresses (A2065 @0x00E90000, A4091 @0x40000000) as
 * longwords.  Robust against kernel-symbol address drift after a relink.
 * Usage: findcfg [hexstart hexend]   (default 0x07800000 0x07900000)
 */
#include <sys/types.h>
#include <stdio.h>
#include <fcntl.h>

int main(argc, argv)
int argc;
char **argv;
{
    int fd;
    unsigned long start = 0x07800000, end = 0x07900000, a;
    unsigned long buf[256];        /* 1KB window of longs */
    unsigned long targets[3];
    int nt = 2, i;

    targets[0] = 0x00E90000UL;     /* A2065 board base */
    targets[1] = 0x40000000UL;     /* A4091 board base */
    if (argc > 2) { start = strtoul(argv[1], 0, 16); end = strtoul(argv[2], 0, 16); }

    if ((fd = open("/dev/kmem", O_RDONLY)) < 0) { perror("/dev/kmem"); return 1; }
    printf("scanning kmem 0x%lx..0x%lx for board addresses\n", start, end);

    for (a = start; a < end; a += sizeof buf) {
        int got;
        if (lseek(fd, a, 0) == -1) continue;
        got = read(fd, (char *)buf, sizeof buf);
        if (got <= 0) continue;
        for (i = 0; i + 4 <= got; i += 4) {
            unsigned long v = buf[i / 4];
            int t;
            for (t = 0; t < nt; t++) {
                if (v == targets[t]) {
                    /* found a cd_BoardAddr; the ConfigDev starts 32 bytes back.
                     * er_Manufacturer is at +20, er_Product at +17, size at +36. */
                    unsigned long cd = a + i - 32;
                    unsigned short mfr;
                    unsigned char prod;
                    unsigned long bsz;
                    unsigned char tmp[68];
                    lseek(fd, (long)cd, 0); read(fd, (char *)tmp, 68);
                    mfr  = (tmp[20] << 8) | tmp[21];
                    prod = tmp[17];
                    bsz  = ((unsigned long)tmp[36] << 24) | (tmp[37] << 16) | (tmp[38] << 8) | tmp[39];
                    printf("board @0x%08lx  cd_BoardAddr=0x%08lx  mfr=0x%04x prod=0x%02x size=0x%08lx  (ConfigDev @0x%08lx)\n",
                           v, v, mfr, prod, bsz, cd);
                    lseek(fd, (long)(a + i + 4), 0);   /* continue */
                }
            }
        }
    }
    return 0;
}
