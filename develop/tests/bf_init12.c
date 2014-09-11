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
		char		gnu;
		unsigned	d:17;
		unsigned	e:2;
		unsigned	f:31;
		int		absurd;
		unsigned	g:1;
	} f = { 123, 2, 6, 1, 66, 0xff2e, 0, 0x4732363, -66666, 1 };
	struct foo	fa[] = {
		{ 33, 1, 12, 2, 77, 0x462, 1, 0xefefef, -3463, 2 },
		{ 123, 2, 3, 1, 99, 0xffe, 0, 0x32363, 25225222, 1 }
	};
	int	i;


	printf("%d\n", (int)sizeof(struct foo));
	printf("%u, %u, %u, %u, %u, %u, %u, %u, %d, %u\n",
		f.a, f.b, f.c, f.d, f.gnu, f.e, f.f, f.g, f.absurd, f.x);

	for (i = 0; i < 2; ++i) {
		printf("%u, %u, %u, %u, %u, %u, %u, %u, %d, %u\n",
			fa[i].a, fa[i].b, fa[i].c, fa[i].d, fa[i].gnu, fa[i].e, fa[i].f, fa[i].g, fa[i].absurd, fa[i].x);
	}
	return 0;
}

