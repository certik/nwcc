#include <stdio.h>

int
main(void) {
	struct garbage {
		int	x;
	} garbagecan[5];
	struct garbage	g;
	struct garbage	*p;
	garbagecan->x = 123;
	p = &g;
	p->x = 456;
	printf("%d %d\n", garbagecan[0].x, garbagecan->x);
#if 0
	garbagecan[1].x = 456;
	printf("%d\n", garbagecan[0].x);
	printf("%d\n", garbagecan[1].x);
#endif
}

