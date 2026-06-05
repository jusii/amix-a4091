/*
 * memread.c -- read N bytes of physical memory via /dev/mem and print hex.
 * Used to characterize the A3000 SCSI WD33C93 region at 0xDD0000 (present vs
 * open-bus) so autocon() can probe it.   cc -O -o memread memread.c
 *   ./memread 0xDD0040 16
 */
#include <stdio.h>
#include <fcntl.h>

main(argc, argv)
int argc; char **argv;
{
	unsigned long addr;
	int fd, i, n;
	unsigned char buf[64];

	addr = 0;
	sscanf(argv[1], "%lx", &addr);
	n = (argc > 2) ? atoi(argv[2]) : 8;
	if (n > 64) n = 64;
	if ((fd = open("/dev/mem", 0)) < 0) { perror("/dev/mem"); return 1; }
	if (lseek(fd, (long)addr, 0) == -1) { perror("lseek"); return 1; }
	n = read(fd, buf, n);
	if (n < 0) { perror("read"); return 1; }
	printf("%lx:", addr);
	for (i = 0; i < n; i++) printf(" %02x", buf[i]);
	printf("\n");
	return 0;
}
