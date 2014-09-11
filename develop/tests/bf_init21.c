#include <stdio.h>

static int func1() { return 1; }
static int func2() { return 2; }
static int func3() { return 3; }

int
main() {
	struct foo {
		int	x:8;
		int	y:8;
		int	z:8;
	} f[] = {
		{ func1(), func2(), func3() },
		{ 1,2,3 },
		{ func3(), func2(), func1() }
	};

	int	i;

	printf("%d\n", (int)sizeof(struct foo));

	for (i = 0; i < 3; ++i) {
		printf("%d %d %d\n", f[i].x, f[i].y, f[i].z);
	}

	return 0;
}

