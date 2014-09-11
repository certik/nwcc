#include <stdio.h>

/*
 * 11/06/08: Test passing of small structs (changed heavily on PowerPC)
 * entirely on the stack
 * There are surrounding args to ensure no GPRs can be used and that
 * nothing is trashed
 */

struct tiny {
	char	c;
};

struct small {
	int	i;
};

struct smallish {
	long long	ll;
};

struct medium {
	char	buf[16];
};


void
foo(int x,
	long dummy0, long dummy1, long dummy2, long dummy3, long dummy4, long dummy5,
	long dummy6, long dummy7, long dummy8,
	struct tiny s, int y) {

	puts("===");
	printf("%d %d %d\n", x, s.c, y);
	printf("%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld\n",
		dummy0, dummy1, dummy2, dummy3, dummy4, dummy5, dummy6, dummy7, dummy8);
}

void
foo2(int x,
	long dummy0, long dummy1, long dummy2, long dummy3, long dummy4, long dummy5,
	long dummy6, long dummy7, long dummy8,

	struct small s, int y) {
	puts("===");
	printf("%d %d %d\n", x, s.i, y);
	printf("%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld\n",
		dummy0, dummy1, dummy2, dummy3, dummy4, dummy5, dummy6, dummy7, dummy8);
}

void
foo3(int x,
	long dummy0, long dummy1, long dummy2, long dummy3, long dummy4, long dummy5,
	long dummy6, long dummy7, long dummy8,
	struct smallish s, int y) {

	puts("===");
	printf("%d %lld %d\n", x, s.ll, y);
	printf("%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld\n",
		dummy0, dummy1, dummy2, dummy3, dummy4, dummy5, dummy6, dummy7, dummy8);
}

void
foo4(int x,
	long dummy0, long dummy1, long dummy2, long dummy3, long dummy4, long dummy5,
	long dummy6, long dummy7, long dummy8,
	struct medium s, int y) {

	puts("===");
	printf("%d %s %d\n", x, s.buf, y);
	printf("%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld\n",
		dummy0, dummy1, dummy2, dummy3, dummy4, dummy5, dummy6, dummy7, dummy8);
}

int
main() {
	struct tiny	y = { 77 };
	struct small	s = { 63444 };
	struct smallish	sm = { 474723747443LL };
	struct medium	m = { "hellohellohello" };

	foo(1, 	9,8,7,6,5,4,3,2,1,	y, 2);
	foo2(3, 19,18,17,16,15,14,13,12,11,	s, 4);
	foo3(5, 109,108,107,106,105,104,103,102,101,	sm, 6);
	foo4(7, 1009,1008,1007,1006,1005,1004,1003,1002,1001,	m, 8);
	return 0;
}

