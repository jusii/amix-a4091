/*
 * checkunix.c -- detect the intermittent ld symtab corruption in an Amix
 * kernel ELF (the "D245 boot-breaker").  Runs natively on the big-endian
 * m68k box, so multi-byte fields are read directly.  Reads an ELF32 file,
 * walks .symtab, and flags any symbol whose st_shndx is out of range
 * (>= e_shnum and not an ELF special index 0xff00..0xffff).  Such a symbol,
 * if referenced by a relocation, makes boot2's rel()/symvaddr() fire
 * Alert(0x52454C41|AT_DeadEnd) == the D245 4C41 Guru.
 *
 * Exit 0 = clean, 1 = CORRUPT, 2 = usage/IO error.
 *   cc -O -o checkunix checkunix.c ; ./checkunix /usr/sys/relocunix
 */
#include <stdio.h>
#include <stdlib.h>

static unsigned char *buf;

/* big-endian field readers (portable regardless of host endianness) */
static unsigned long be32(off) unsigned long off; {
    return ((unsigned long)buf[off]<<24)|((unsigned long)buf[off+1]<<16)|
           ((unsigned long)buf[off+2]<<8)|(unsigned long)buf[off+3];
}
static unsigned int be16(off) unsigned long off; {
    return (buf[off]<<8)|buf[off+1];
}

main(argc, argv)
int argc; char **argv;
{
    FILE *f;
    long sz;
    unsigned long e_shoff, shoff, syoff, sysz, syent, stroff;
    unsigned int e_shnum, e_shentsize, i, shtype;
    unsigned long nsyms, k, so, shndx, bad = 0, nrelbad = 0;

    if (argc < 2) { fprintf(stderr, "usage: checkunix <elf>\n"); exit(2); }
    if ((f = fopen(argv[1], "r")) == NULL) { perror(argv[1]); exit(2); }
    fseek(f, 0L, 2); sz = ftell(f); fseek(f, 0L, 0);
    if ((buf = (unsigned char *)malloc(sz)) == NULL) { fprintf(stderr,"oom\n"); exit(2); }
    if (fread(buf, 1, sz, f) != sz) { fprintf(stderr,"short read\n"); exit(2); }
    fclose(f);
    if (buf[0]!=0x7f||buf[1]!='E'||buf[2]!='L'||buf[3]!='F') { fprintf(stderr,"not ELF\n"); exit(2); }

    e_shoff = be32(32L);
    e_shentsize = be16(46L);
    e_shnum = be16(48L);
    /* locate .symtab (SHT_SYMTAB==2) */
    syoff = 0;
    for (i = 0; i < e_shnum; i++) {
        shoff = e_shoff + (unsigned long)i*e_shentsize;
        shtype = be32(shoff+4L);
        if (shtype == 2) {
            syoff = be32(shoff+16L);
            sysz  = be32(shoff+20L);
            syent = be32(shoff+36L);
        }
    }
    if (syoff == 0) { fprintf(stderr,"no symtab\n"); exit(2); }
    if (syent == 0) syent = 16;
    nsyms = sysz / syent;

    for (k = 0; k < nsyms; k++) {
        so = syoff + k*syent;
        shndx = be16(so+14L);
        if (shndx != 0 && shndx < 0xff00 && shndx >= e_shnum) {
            if (bad < 12)
                printf("  sym[%lu] st_shndx=%lu (out of range, e_shnum=%u)\n",
                       k, shndx, e_shnum);
            bad++;
        }
    }
    if (bad) {
        printf("CORRUPT: %s has %lu symbol(s) with out-of-range st_shndx\n", argv[1], bad);
        exit(1);
    }
    printf("OK: %s symtab clean (%lu syms, e_shnum=%u)\n", argv[1], nsyms, e_shnum);
    exit(0);
}
