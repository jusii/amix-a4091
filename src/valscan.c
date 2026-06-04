/* valscan.c -- scan /dev/kmem for a 32-bit value; dump 40 bytes of context.
 * Usage: valscan <hexvalue> [hexstart hexend]
 */
#include <sys/types.h>
#include <stdio.h>
#include <fcntl.h>

int main(argc, argv)
int argc;
char **argv;
{
    int fd, got, i, hits = 0;
    unsigned long start = 0x07000000, end = 0x08000000, a, target;
    unsigned long buf[256];
    char *path = "/dev/kmem";

    if (argc < 2) { fprintf(stderr, "usage: valscan hexval [start end] [path]\n"); return 2; }
    target = strtoul(argv[1], 0, 16);
    if (argc > 3) { start = strtoul(argv[2], 0, 16); end = strtoul(argv[3], 0, 16); }
    if (argc > 4) path = argv[4];

    if ((fd = open(path, O_RDONLY)) < 0) { perror(path); return 1; }
    printf("scanning %s 0x%lx..0x%lx for 0x%08lx\n", path, start, end, target);
    for (a = start; a < end; a += sizeof buf) {
        lseek(fd, (long)a, 0);
        got = read(fd, (char *)buf, sizeof buf);
        if (got <= 0) continue;
        for (i = 0; i + 4 <= got; i += 4) {
            if (buf[i / 4] == target) {
                int k;
                unsigned char ctx[40];
                printf("hit @0x%08lx:", a + i);
                lseek(fd, (long)(a + i), 0);
                if (read(fd, (char *)ctx, 40) == 40)
                    for (k = 0; k < 40; k += 4)
                        printf(" %02x%02x%02x%02x", ctx[k], ctx[k+1], ctx[k+2], ctx[k+3]);
                printf("\n");
                if (++hits >= 12) return 0;
            }
        }
    }
    printf("%d hit(s)\n", hits);
    return 0;
}
