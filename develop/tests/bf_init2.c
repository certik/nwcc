#include <stdio.h>

int
main() {
	/* Check whether basic merging works */
	struct foo {
		unsigned	x:1;
		unsigned	y:1;
		unsigned	z:5;
		unsigned	a:3;
		unsigned	b:8;
	} f = { 1, 0, 7, 2, 120 };
			struct dummy {
				unsigned long	x;
			} d = { 0 };  /* align initializer storage */
	struct foo f2 = { 1, 1, 0x1f, 0x7, 0xff };
			struct dummy d2 = { 0 }; /* align initializer storage */
	struct foo f3[] = {
		{ 1, 0, 7, 2, 120 },
		{ 0, 1, 0x1f, 0x7, 0xff }
	};
	int	i;


	printf("%d\n", (int)sizeof(struct foo));
	printf("%d, %d, %d, %d, %d\n", f.x, f.y, f.z, f.a, f.b);
	printf("%d, %d, %d, %d, %d\n", f2.x, f2.y, f2.z, f2.a, f2.b);
	for (i = 0; i < 2; ++i) {
		printf("%d, %d, %d, %d, %d\n", f3[i].x, f3[i].y, f3[i].z, f3[i].a, f3[i].b);
	}
	return 0;
}

