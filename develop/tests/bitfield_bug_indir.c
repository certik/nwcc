#include <stdio.h>

struct foo {
	unsigned int	x:1;
	unsigned int	y:1;
};

int
main() {
	struct foo	bar;
	struct foo	*p;

	p = &bar;
	bar.x = 0;
	bar.y = 0;

	/*
	 * 05/25/09: TERRIBLE! We didn't handle bitfield assignment for
	 * indirect struct access correctly!
	 */
	printf("%d %d\n", bar.x, bar.y);
	p->x = 1;
	printf("%d %d\n", bar.x, bar.y);
	p->y = 1;
	printf("%d %d\n", bar.x, bar.y);
	p->y = 0;
	printf("%d %d\n", bar.x, bar.y);
	return 0;
}

