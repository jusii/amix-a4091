/* SPDX-License-Identifier: MIT */
/* strscan.c -- search kernel memory (/dev/kmem) for an ASCII string.
 * Usage: strscan "needle" [hexstart hexend]
 */
#include <sys/types.h>
#include <stdio.h>
#include <fcntl.h>

int main(argc, argv)
int argc;
char **argv;
{
    int fd, n, i, j, hits = 0;
    unsigned long start = 0x07800000, end = 0x07a00000, a;
    char buf[4096];
    char *needle = (argc > 1) ? argv[1] : "A4091 SCSI";
    int nl = 0;

    while (needle[nl]) nl++;
    if (argc > 3) { start = strtoul(argv[2], 0, 16); end = strtoul(argv[3], 0, 16); }

    if ((fd = open("/dev/kmem", O_RDONLY)) < 0) { perror("/dev/kmem"); return 1; }
    printf("searching kmem 0x%lx..0x%lx for \"%s\"\n", start, end, needle);

    for (a = start; a < end; a += sizeof buf - 64) {
        lseek(fd, (long)a, 0);
        n = read(fd, buf, sizeof buf);
        if (n <= 0) continue;
        for (i = 0; i + nl <= n; i++) {
            for (j = 0; j < nl; j++)
                if (buf[i + j] != needle[j]) break;
            if (j == nl) {
                int k; char c;
                printf("  hit @0x%08lx: \"", a + i);
                for (k = 0; k < 96 && i + k < n; k++) {
                    c = buf[i + k];
                    putchar((c >= 32 && c < 127) ? c : (c == '\n' ? '\n' : '.'));
                }
                printf("\"\n");
                if (++hits >= 20) { printf("  (stopping at 20)\n"); return 0; }
            }
        }
    }
    printf("%d hit(s)\n", hits);
    return 0;
}
