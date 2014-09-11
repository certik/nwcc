#include <stdio.h>

int
main() {
	struct foo {
		int	x;
		int	y;
		int	z;
	} ar[]  = {
		1, 2, 3, 4, 5, 6, 7, 8, 9
	};
	int i;
	for (i = 0; i < 3; ++i) {
		printf("%d %d %d\n", ar[i].x, ar[i].y, ar[i].z);
	}
	return 0;
}

