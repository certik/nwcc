#include <stdio.h>

int
main() {
	struct foo {
		int	x;
		char	*p;
		int	y[2];
		int	implicit;
	} bar[] = {
		{ 123, "hello", { 456, 678 } },
		{ 322, "hetae", { 362, 544 } },
		{ 4362, "hrhyelo", { 311, 422 }, },
		{ 4362, "heelo", { 311, 422 } }
	};	
	int	i;

	for (i = 0; i < 4; ++i) {
		printf("%d, %s, %d, %d\n",
			bar[i].x, bar[i].p, bar[i].y[0], bar[i].y[1],
			bar[i].implicit);	
	}
}

