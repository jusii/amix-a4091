/* SPDX-License-Identifier: MIT */
/* z3probe.c -- can Amix userspace reach the A4091's Zorro III window at all?
 *
 * The A4091 AutoConfigured at physical 0x40000000 (16 MB), per bootinfo.
 * This mmaps /dev/mem over that window (exactly how lszorro reaches Zorro II)
 * and reads it.  mmap defers the real bus access to userspace, so an
 * unreachable address raises SIGBUS in THIS process (caught) rather than
 * panicking the kernel.  We dump the head of the window and scan for the
 * A4091 autoboot-ROM signature ("A4091"/"Commodore"), which both proves
 * reachability and confirms the board.
 *
 * Build: cc -o z3probe z3probe.c
 * Run as root:  ./z3probe [hexbase] [hexlen]
 */
#include <sys/types.h>
#include <sys/mman.h>
#include <stdio.h>
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>

static jmp_buf jb;
static volatile int busfault;

static void onbus(sig)
int sig;
{
    busfault = 1;
    longjmp(jb, 1);
}

int main(argc, argv)
int argc;
char **argv;
{
    int fd, i, ok, bad;
    unsigned long base = 0x40000000L;
    unsigned long len  = 0x1000;             /* one 4K page by default */
    volatile unsigned char *p;
    caddr_t map;

    if (argc > 1) base = strtoul(argv[1], (char **)0, 16);
    if (argc > 2) len  = strtoul(argv[2], (char **)0, 16);

    if ((fd = open("/dev/mem", O_RDONLY)) < 0) { perror("/dev/mem"); return 1; }

    map = mmap((caddr_t)0, (size_t)len, PROT_READ, MAP_SHARED, fd, (off_t)base);
    if (map == (caddr_t)-1) {
        perror("mmap");
        printf("mmap of phys 0x%lx FAILED -> mem driver rejects this address\n", base);
        return 2;
    }
    printf("mmap ok: phys 0x%lx len 0x%lx -> va %p\n", base, len, (void *)map);
    p = (volatile unsigned char *)map;

    signal(SIGBUS, onbus);
    signal(SIGSEGV, onbus);

    /* Probe the very first long with fault protection. */
    if (setjmp(jb)) {
        printf("\n*** BUS ERROR on first access -> Zorro III 0x%lx is NOT reachable\n", base);
        printf("    (the kernel's MMU does not map this physical space)\n");
        return 3;
    }
    {
        unsigned long first = *(volatile unsigned long *)p;
        printf("first long @0x%lx = 0x%08lx  (read SUCCEEDED)\n\n", base, first);
    }

    /* Hex+ascii dump of the head of the window (fault-protected per row). */
    printf("hexdump of 0x%lx:\n", base);
    for (i = 0; i < (int)len && i < 256; i += 16) {
        int j;
        if (setjmp(jb)) { printf("  +0x%04x: <bus error>\n", i); continue; }
        printf("  +0x%04x: ", i);
        for (j = 0; j < 16; j++) printf("%02x ", p[i + j]);
        printf(" ");
        for (j = 0; j < 16; j++) {
            unsigned char c = p[i + j];
            putchar((c >= 0x20 && c < 0x7f) ? c : '.');
        }
        putchar('\n');
    }

    /* Scan whole window for printable strings >= 4 chars (find "A4091" etc). */
    printf("\nASCII strings in window:\n");
    ok = bad = 0;
    {
        char buf[80];
        int run = 0;
        for (i = 0; i < (int)len; i++) {
            unsigned char c;
            if (setjmp(jb)) { bad++; run = 0; continue; }
            c = p[i];
            ok++;
            if (c >= 0x20 && c < 0x7f && run < (int)sizeof(buf) - 1) {
                buf[run++] = c;
            } else {
                if (run >= 4) { buf[run] = 0; printf("  +0x%04x: \"%s\"\n", i - run, buf); }
                run = 0;
            }
        }
    }
    printf("\naccessible bytes: %d, bus-faulted bytes: %d\n", ok, bad);
    return 0;
}
