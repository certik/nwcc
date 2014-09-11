#include <stdio.h>

int
main() {
	/* Check with more than one storage unit, and whether
	 * mixing with ordinary char at end works and still
	 * aligns properly
	 */
	struct foo {
		char		x; /* align me */
		unsigned	a:3;
		unsigned	b:4;
		unsigned	c:3;
		unsigned	d:17;
		unsigned	e:2;
		unsigned	f:31;
		unsigned	g:1;
	} f = { 123, 2, 6, 1, 0xff2e, 0, 0x4732363, 1 };
	struct foo	fa[] = {
		{ 33, 1, 12, 2, 0x462, 1, 0xefefef, 2 },
		{ 123, 2, 3, 1, 0xffe, 0, 0x32363, 1 }
	};
	int	i;


	printf("%d\n", (int)sizeof(struct foo));
	printf("%u, %u, %u, %u, %u, %u, %u, %u\n",
		f.a, f.b, f.c, f.d, f.e, f.f, f.g, f.x);

	for (i = 0; i < 2; ++i) {
		printf("%u, %u, %u, %u, %u, %u, %u, %u\n",
			fa[i].a, fa[i].b, fa[i].c, fa[i].d, fa[i].e, fa[i].f, fa[i].g, fa[i].x);
	}
	return 0;
}

