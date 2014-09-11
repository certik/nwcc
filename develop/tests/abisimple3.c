#include <stdio.h>

/*
 * 11/06/08: Test passing of small structs (changed heavily on PowerPC)
 * There are surrounding args to ensure those are not trashed
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
foo(int x, struct tiny s, int y) {
	puts("===");
	printf("%d %d %d\n", x, s.c, y);
}

void
foo2(int x, struct small s, int y) {
	puts("===");
	printf("%d %d %d\n", x, s.i, y);
}

void
foo3(int x, struct smallish s, int y) {
	puts("===");
	printf("%d %lld %d\n", x, s.ll, y);
}

void
foo4(int x, struct medium s, int y) {
	puts("===");
	printf("%d %s %d\n", x, s.buf, y);
}

int
main() {
	struct tiny	y = { 77 };
	struct small	s = { 63444 };
	struct smallish	sm = { 474723747443LL };
	struct medium	m = { "hellohellohello" };

	foo(1, y, 2);
	foo2(3, s, 4);
	foo3(5, sm, 6);
	foo4(7, m, 8);
	return 0;
}

