#include <stdio.h>

/*
 * 05/20/11: This demonstrates another x86/AMD64 code reuse bug:
 * The shift operation somehow involves a 32bit register in the
 * x86 code paths, which do not account for the fact that we
 * are dealing with 64bit items on AMD64
 */
int
main()
{
        int count = 0;
        unsigned long gnu = 32;
        unsigned long foo = 2;
        gnu >>= foo;
        printf("%lu\n", gnu);
        return count;
}

