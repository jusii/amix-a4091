/* SPDX-License-Identifier: MIT */
/* z3scan.c -- map the A4091's Zorro III window and find its populated regions
 * (autoboot ROM, 53C710 registers) by scanning for non-0xFF/0x00 content and
 * ASCII strings.  Block-based with per-block SIGBUS protection so it is both
 * fast and panic-free.
 *
 * Build: cc -o z3scan z3scan.c
 * Run as root:  ./z3scan [hexbase] [hexlen]   (default 0x40000000 0x1000000)
 */
#include <sys/types.h>
#include <sys/mman.h>
#include <stdio.h>
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>

#define BLK 4096

static jmp_buf jb;
static void onbus(sig) int sig; { longjmp(jb, 1); }

int main(argc, argv)
int argc;
char **argv;
{
    int fd;
    unsigned long base = 0x40000000L, len = 0x1000000L, off;
    int contentchunks = 0, unmapped = 0;
#define CHUNK 0x10000L                       /* map 64K at a time */

    if (argc > 1) base = strtoul(argv[1], (char **)0, 16);
    if (argc > 2) len  = strtoul(argv[2], (char **)0, 16);

    if ((fd = open("/dev/mem", O_RDONLY)) < 0) { perror("/dev/mem"); return 1; }
    printf("scanning phys 0x%lx .. 0x%lx in 0x%lx chunks\n", base, base + len, CHUNK);
    signal(SIGBUS, onbus);
    signal(SIGSEGV, onbus);

    for (off = 0; off < len; off += CHUNK) {
        caddr_t map;
        volatile unsigned char *m;
        unsigned long b;
        int nff = 0, n00 = 0, nother = 0, run = 0; char sbuf[80];

        map = mmap((caddr_t)0, (size_t)CHUNK, PROT_READ, MAP_SHARED, fd, (off_t)(base + off));
        if (map == (caddr_t)-1) { unmapped++; continue; }   /* mem driver rejects this page */
        m = (volatile unsigned char *)map;

        for (b = 0; b < CHUNK; b += BLK) {
            int i;
            if (setjmp(jb)) continue;        /* block bus-faulted; skip it */
            for (i = 0; i < BLK; i++) {
                unsigned char c = m[b + i];
                if (c == 0xff) nff++; else if (c == 0x00) n00++; else nother++;
                if (c >= 0x20 && c < 0x7f && run < (int)sizeof(sbuf) - 1) sbuf[run++] = c;
                else { if (run >= 4) { sbuf[run] = 0;
                           printf("  str @0x%08lx: \"%s\"\n", base + off + b + i - run, sbuf); }
                       run = 0; }
            }
        }
        if (nother > 0) {                    /* real content (not just ff/00 fill) */
            contentchunks++;
            printf("  CHUNK @0x%08lx: ff=%d 00=%d other=%d\n", base + off, nff, n00, nother);
        }
        munmap(map, (size_t)CHUNK);
    }
    printf("\nsummary: %d content chunks, %d unmapped chunks, %ld total\n",
           contentchunks, unmapped, (long)(len / CHUNK));
    return 0;
}
