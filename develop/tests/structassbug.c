#include <stdio.h>

struct foo {
	int x;
};

struct bar {
	struct foo	f;
	struct foo	f2;
	struct foo	f3;
};

int
main() {
	struct bar	b;
	struct bar	*bp;
	struct foo	src;

	src.x = 123;

	bp = &b;
	bp->f3 = src;
	sync();
	bp->f = bp->f2 = bp->f3;
	sync();
	printf("%d %d %d\n", b.f.x, b.f2.x, b.f3.x);
}


