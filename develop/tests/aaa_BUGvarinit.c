#include <stdio.h>


static int	func1() { return 1; }
static int	func2() { return 2; }
static int	func3() { return 3; }
static int	func6() { return func3()+func2()+func1(); }

int
main() {
	/* Check with more than one storage unit, and whether
	 * mixing with ordinary char at end works and still
	 * aligns properly
	 *
	 * Mix with variable initializers too
	 */
	struct foo {
		char		x; /* align me */
		unsigned	a;
		unsigned	b;
		unsigned	c;
		char		gnu;
		unsigned	d;
		unsigned	e;
		unsigned	f;
		int		absurd;
		unsigned	g;
	} fa[] = {
		{ 0 },
		{ 1 },
		{ 1, 2 },
		{ 1, 2, func3() },
		{ 1, 2, 3, 4 },
		{ 1, 2, 3, 4, 5 },
		{ 1, func2(), 3, 4, 5, 6 },
		{ func1(), 2, 3, 4, 5, 6, 3 },
		{ 1, 2, 3, 4, 5, 6, 3, 8 },
		{ 1, 2, 3, 4, 5, func6(), 3, 8, 9 },
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

