#include <stdio.h>
#include <string.h>


void
dumpbits(void *p, int size) {
        unsigned char   *ucp = p;
        int             i;

        for (i = 0; i < size; ++i) {
                printf("%02x ", *ucp++);
        }
        putchar('\n');
}

/*
 * 06/01/11: This test case exposes the shocking and very fundamental
 * (and as of yet)  * unknown fact that bitfield storage is completely
 * wrong for PowerPC and possibly all other big endian targets.
 *
 * "y" ends up being stored in bits that are already occupied by "x"
 * on that (or those) platform(s). There are a few storage layout test
 * cases in the test suite, but they probably weren't rigorous enough
 * to detect this.
 *
 * What should happen in this test is that x comes first, followed by
 * y - with endianness making a difference as to where the most
 * significant bits of "x" are stored on little/big endian systems
 * (y always lives in a byte, endianness-independently)
 *
 * Ideally all bitfield storage asssignment code should be rewritten
 * because it is laughably complex for such a (supposedly) simple
 * task
 */

int
main() {
        struct foo {
                unsigned x:30;
                unsigned y:2;
        } f;
        unsigned z;

        memset(&f, 0, sizeof f);
        f.x = 123;
        dumpbits(&f, sizeof f);
        memset(&f, 0, sizeof f);
        f.y = 1;
        dumpbits(&f, sizeof f);
}

