/* kmemdump.c -- hex-dump kernel memory via /dev/kmem.
 * Usage: kmemdump <hexaddr> <declen>
 */
#include <sys/types.h>
#include <stdio.h>
#include <fcntl.h>

int main(argc, argv)
int argc;
char **argv;
{
    int fd, i, n;
    long addr;
    unsigned char buf[1024];
    int len;

    if (argc < 3) { fprintf(stderr, "usage: kmemdump hexaddr declen\n"); return 2; }
    addr = strtoul(argv[1], (char **)0, 16);
    len  = atoi(argv[2]);
    if (len > (int)sizeof buf) len = sizeof buf;

    if ((fd = open("/dev/kmem", O_RDONLY)) < 0) { perror("/dev/kmem"); return 1; }
    if (lseek(fd, addr, 0) == -1) { perror("lseek"); return 1; }
    n = read(fd, (char *)buf, len);
    if (n <= 0) { perror("read"); return 1; }
    for (i = 0; i < n; i++) {
        if (i % 16 == 0) printf("\n0x%08lx: ", addr + i);
        printf("%02x ", buf[i]);
    }
    printf("\n");
    /* also as longwords for pointer/id inspection */
    printf("longs:");
    for (i = 0; i + 4 <= n; i += 4) {
        unsigned long v = ((unsigned long)buf[i] << 24) | ((unsigned long)buf[i+1] << 16)
                        | ((unsigned long)buf[i+2] << 8) | buf[i+3];
        if (i % 16 == 0) printf("\n0x%08lx: ", addr + i);
        printf("%08lx ", v);
    }
    printf("\n");
    return 0;
}
