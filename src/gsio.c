/* SPDX-License-Identifier: MIT */
/* gsio.c -- submit a raw SCSI command to an Amix host adapter via the GSIO
 * passthrough ( /dev/scsi, cdevsw 11, scsi.c:gsioctl ).  Sends INQUIRY by
 * default.  Reusable for the A4091 once a4091queue is registered (just point
 * at the right card index).
 *
 * Build on the box (uses the real kernel struct sdcom so layout matches):
 *     cc -I/usr/sys/amiga/alien -o gsio gsio.c
 * Run as root:  ./gsio [card] [unit] [cdbhex...]
 *   default: card 0 unit 6  -> INQUIRY to the A3000 disk at SCSI id 6
 */
#include <sys/types.h>
#include <stdio.h>
#include <fcntl.h>
#include "rico.h"
#include "sd.h"

#define GSIO 'GSIO'                 /* the gsioctl command */

int main(argc, argv)
int argc;
char **argv;
{
    int fd, i, n;
    struct sdcom sc;
    unsigned char buf[1024];        /* must hold a 512-byte block; <= scsi.c iobuf */
    int card = (argc > 1) ? atoi(argv[1]) : 0;
    int unit = (argc > 2) ? atoi(argv[2]) : 6;

    for (i = 0; i < (int)sizeof sc; i++) ((char *)&sc)[i] = 0;
    for (i = 0; i < (int)sizeof buf; i++) buf[i] = 0;

    sc.card    = card;
    sc.unit    = unit;
    sc.reading = TRUE;
    sc.addr    = (caddr_t)buf;
    if (argc > 3) {                 /* explicit CDB bytes given */
        for (i = 3, n = 0; i < argc && n < 12; i++, n++)
            sc.cdb[n] = (uchar)strtol(argv[i], (char **)0, 16);
        switch (sc.cdb[0]) {        /* data length + direction by opcode */
        case 0x28: sc.nbyte = (((sc.cdb[7]<<8)|sc.cdb[8]) * 512); break; /* READ(10) */
        case 0x2a:                                 /* WRITE(10): send a test pattern */
            sc.nbyte = (((sc.cdb[7]<<8)|sc.cdb[8]) * 512);
            sc.reading = FALSE;
            for (i = 0; i < (int)sc.nbyte && i < (int)sizeof buf; i++)
                buf[i] = (unsigned char)(0xA0 + (i & 0x1f));
            break;
        case 0x25: sc.nbyte = 8;          break;   /* READ CAPACITY */
        case 0x03: sc.nbyte = sc.cdb[4];  break;   /* REQUEST SENSE */
        default:   sc.nbyte = 36;         break;
        }
        if (sc.nbyte == 0 || sc.nbyte > (int)sizeof buf) sc.nbyte = sizeof buf;
    } else {                        /* default: INQUIRY */
        sc.cdb[0] = 0x12;           /* INQUIRY */
        sc.cdb[4] = 36;             /* allocation length */
        sc.nbyte  = 36;
    }

    if ((fd = open("/dev/scsi", O_RDWR)) < 0) { perror("/dev/scsi"); return 1; }
    printf("GSIO card=%d unit=%d cdb=", card, unit);
    for (i = 0; i < 6; i++) printf("%02x ", sc.cdb[i]);
    printf("\n");

    if (ioctl(fd, GSIO, &sc) < 0) { perror("ioctl GSIO"); return 1; }

    printf("-> status=0x%02x okay=%d nbyte=%u\n", sc.status, sc.okay, sc.nbyte);
    printf("data:");
    for (i = 0; i < (int)sc.nbyte && i < (int)sizeof buf; i++) {
        if (i % 16 == 0) printf("\n  +%02x: ", i);
        printf("%02x ", buf[i]);
    }
    printf("\n");
    if (sc.okay && sc.cdb[0] == 0x12) {       /* decode INQUIRY */
        char vid[9], pid[17], rev[5];
        for (i = 0; i < 8; i++)  vid[i] = buf[8 + i];  vid[8] = 0;
        for (i = 0; i < 16; i++) pid[i] = buf[16 + i]; pid[16] = 0;
        for (i = 0; i < 4; i++)  rev[i] = buf[32 + i]; rev[4] = 0;
        printf("INQUIRY: devtype=0x%02x  vendor=\"%s\"  product=\"%s\"  rev=\"%s\"\n",
               buf[0] & 0x1f, vid, pid, rev);
    }
    if (sc.okay && sc.cdb[0] == 0x28) {       /* READ(10): flag an Amiga RDB */
        printf("first longword = %02x%02x%02x%02x", buf[0], buf[1], buf[2], buf[3]);
        if (buf[0]=='R' && buf[1]=='D' && buf[2]=='S' && buf[3]=='K')
            printf("  <- 'RDSK' RigidDiskBlock!");
        printf("\n");
    }
    return 0;
}
