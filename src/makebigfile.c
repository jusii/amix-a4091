/* SPDX-License-Identifier: MIT */
/*
 * makebigfile.c -- probe whether the intermittent kernel-image corruption
 * (NOTES s18) is triggered by ld's I/O *pattern* (small buffered writes +
 * seek-back patching) or is internal to ld.  Builds a ~1.5 MB file with
 * deterministic content using a write-then-seek-and-patch pattern similar to
 * how ld lays down sections then fixes up symtab/relocations.  The content is
 * fully deterministic, so the output `sum` is constant UNLESS the write path
 * corrupts it.  Run it many times; if sums vary, a non-ld program reproduces
 * the fault => it's the FS/disk/emulator I/O path, not ld internals.
 *
 *   cc -O -o makebigfile makebigfile.c
 *   n=1; while [ $n -le 20 ]; do ./makebigfile /tmp/bt; sum -r /tmp/bt; n=`expr $n + 1`; done
 */
#include <stdio.h>

#define N 1500000L

main(argc, argv)
int argc; char **argv;
{
    FILE *f;
    long i;
    char *path;

    path = (argc > 1) ? argv[1] : "/tmp/bigtest";
    if ((f = fopen(path, "w")) == NULL) { perror(path); return 2; }
    /* phase 1: sequential, byte-at-a-time buffered writes (fills blocks) */
    for (i = 0; i < N; i++)
        putc((int)((i * 7 + (i >> 8)) & 0xff), f);
    /* phase 2: seek back and patch many small spots, like reloc/symtab fixups */
    for (i = 0; i < N; i += 24) {
        fseek(f, i, 0);
        putc((int)((i ^ 0x5a) & 0xff), f);
    }
    fflush(f);
    fclose(f);
    return 0;
}
