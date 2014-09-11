#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

/*
 * 07/21/08: This test case does the same thing as struct-pass-small, except
 * that the struct is 9 or 17 bytes long instead of 2 bytes. This is to
 * ensure that alignment is correctly dealt with in this case as well
 */
struct tiny {
	char c;
	long dummy;
	char d;
};

/*
 * Check that small structs are passed correctly (aligned to dword
 * boundary on both sides) via stdarg
 */
void
f(int n, ...);

/*
 * Check that small structs are passed correctly (aligned to dword
 * boundary on both sides)
 */
void
f2(int n, struct tiny x1, struct tiny x2, struct tiny x3, ...);

/*
 * Check that small structs are passed correctly (aligned to dword
 * boundary on both sides) even with intervening (and alignment-
 * changing) small types
 */
void
f3(int n, char dummy, struct tiny x1, char dummy2, struct tiny x2, short dummy3, long foo, struct tiny x3, ...);

/*
 * Check that small structs are passed correctly (aligned to dword
 * boundary on both sides) even with intervening (and alignment-
 * changing) small types
 */
void
f4(int n, ...);

int
main ()
{
	struct tiny x[3];

	x[0].c = 10;
	x[1].c = 11;
	x[2].c = 12;
	x[0].d = 20;
	x[1].d = 21;
	x[2].d = 22;

	printf("%d, %d\n", x[0].c, x[0].d);
	printf("%d, %d\n", x[1].c, x[1].d);
	printf("%d, %d\n", x[2].c, x[2].d);
	puts("===");

	f(3, x[0], x[1], x[2], (long)123);
	f2(3, x[0], x[1], x[2], (long)123);
	f3(3,    11,   x[0],   22,    x[1],   33,    77777,  x[2], (long)123);
	f4(0, 1/*char*/, x[0], 2L, 3.4, x[1], (char)5, (short)6, 7LL, x[2]);
	exit(0);
}

