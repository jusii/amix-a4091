/* cc-smoke.c -- smoke-test the native Amix build loop and report the C ABI
 * we will be writing the 53C710 driver against. Build natively on the box:
 *     cc -o cc-smoke cc-smoke.c
 */
#include <stdio.h>

int main()
{
    unsigned long probe = 0x01020304UL;
    unsigned char *p = (unsigned char *)&probe;
    int big = (p[0] == 0x01);

    printf("amix cc-smoke: ok\n");
    printf("  sizeof(char/short/int/long/ptr) = %d/%d/%d/%d/%d\n",
           (int)sizeof(char), (int)sizeof(short), (int)sizeof(int),
           (int)sizeof(long), (int)sizeof(void *));
    printf("  byte order = %s\n", big ? "big-endian (68k)" : "little-endian");
    printf("  __STDC__ = %d\n",
#ifdef __STDC__
           __STDC__
#else
           -1
#endif
        );
    return 0;
}
