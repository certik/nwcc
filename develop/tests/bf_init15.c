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
	} fa[] = {
		{ 0 },
		{ 1 },
		{ 1, 2 },
		{ 1, 2, 3 },
		{ 1, 2, 3, 4 },
		{ 1, 2, 3, 4, 5 },
		{ 1, 2, 3, 4, 5, 6 },
		{ 1, 2, 3, 4, 5, 6, 3 },
		{ 1, 2, 3, 4, 5, 6, 3, 8 },
		{ 1, 2, 3, 4, 5, 6, 3, 8, 9 },
		{ 1, 2, 3, 4, 5, 6, 3, 8, 9, 1 },
	};
	int	i;


	printf("%d\n", (int)sizeof(struct foo));

	for (i = 0; i < 11; ++i) {
		printf("%u, %u, %u, %u, %u, %u, %u, %u, %d, %u\n",
			fa[i].a, fa[i].b, fa[i].c, fa[i].d, fa[i].gnu, fa[i].e, fa[i].f, fa[i].g, fa[i].absurd, fa[i].x);
	}
	return 0;
}

