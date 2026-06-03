/* dumpcfg.c -- dump the kernel's bootinfo.autocon[] AutoConfig table from
 * /dev/kmem, to see every board the AmigaOS bootstrap handed to the Amix
 * kernel -- crucially including any Zorro III board (e.g. the A4091), whose
 * cd_BoardAddr autocon() returns verbatim with no Zorro II/III filtering.
 *
 * Build natively:  cc -o dumpcfg dumpcfg.c
 * Run as root:     ./dumpcfg            (reads /dev/kmem + /unix symbols)
 *
 * struct ConfigDev layout copied verbatim from /usr/sys/amiga/kernel/support.c.
 */
#include <sys/types.h>
#include <stdio.h>
#include <fcntl.h>

#define NAUTO 16

/*
 * bootinfo runtime kernel-virtual address.
 *  - kernel bound at 0x07c00000 (amiga/config/A3000.mapfile)
 *  - sections packed contiguously by the loader (boot/rel.c bindsections),
 *    this kernel has no .rodata:  .text 0x07c00000 (size 0xd7184)
 *    -> .data base 0x07cd7184; bootinfo at .data + 0x4784 = 0x07cdb908.
 * The current /stand/unix is ET_REL so nlist() can't give this; recompute
 * (and pass as argv[1]) after any kernel relink.
 */
#define BOOTINFO_KVA 0x07cdb908L

struct ConfigDev {
    struct ConfigDev *ln_Succ;
    struct ConfigDev *ln_Pred;
    unchar  ln_Type;
    char    ln_Pri;
    ushort  ln_Name[2];
    unchar  cd_Flags;
    unchar  cd_Pad;
    unchar  er_Type;
    unchar  er_Product;
    unchar  er_Flags;
    unchar  er_Reserved03;
    ushort  er_Manufacturer;
    ushort  er_SerialNumber[2];
    ushort  er_InitDiagVec;
    unchar  er_Reserved0c;
    unchar  er_Reserved0d;
    unchar  er_Reserved0e;
    unchar  er_Reserved0f;
    ulong   cd_BoardAddr;
    ulong   cd_BoardSize;
    ushort  cd_SlotAddr;
    ushort  cd_SlotSize;
    ulong   cd_Driver;
    struct ConfigDev *cd_NextCD;
    ulong   cd_Unused[4];
};

int main(argc, argv)
int argc;
char **argv;
{
    int kmem, i, n;
    long addr;
    struct ConfigDev cd[NAUTO];

    addr = BOOTINFO_KVA;
    if (argc > 1)
        addr = strtoul(argv[1], (char **)0, 16);

    printf("dumpcfg: sizeof(struct ConfigDev) = %d (expect 68)\n",
           (int)sizeof(struct ConfigDev));
    printf("reading bootinfo from /dev/kmem @ 0x%lx\n", addr);

    if ((kmem = open("/dev/kmem", O_RDONLY)) < 0) {
        perror("/dev/kmem");
        return 1;
    }
    if (lseek(kmem, addr, 0) == -1) { perror("lseek"); return 1; }
    n = read(kmem, (char *)cd, sizeof cd);
    if (n != (int)sizeof cd) {
        fprintf(stderr, "short read from kmem: %d of %d\n", n, (int)sizeof cd);
        /* keep going with whatever we got */
    }
    close(kmem);

    printf("\nslot  mfr    prod  erType  boardAddr   boardSize   bus\n");
    printf("----  -----  ----  ------  ----------  ----------  --------\n");
    for (i = 0; i < NAUTO; i++) {
        ushort mfr  = cd[i].er_Manufacturer;
        unchar prod = cd[i].er_Product;
        ulong  ba   = cd[i].cd_BoardAddr;
        ulong  bs   = cd[i].cd_BoardSize;
        char  *bus;
        if (mfr == 0 && ba == 0 && bs == 0)
            continue;                       /* empty slot */
        /* Zorro II lives below 16 MB; Zorro III is up at >= 0x10000000 */
        bus = (ba >= 0x10000000UL) ? "ZORRO-III" :
              (ba >= 0x00E80000UL && ba < 0x01000000UL) ? "ZorroII-io" :
              (ba >= 0x00200000UL && ba < 0x00A00000UL) ? "ZorroII-mem" : "low/other";
        printf("%4d  %5u  %4u  0x%02x    0x%08lx  0x%08lx  %s\n",
               i, mfr, prod, cd[i].er_Type, ba, bs, bus);
    }
    return 0;
}
