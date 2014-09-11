#include <stdio.h>

int
main() {
	struct foo {
		int	x;
		int	y;
		int	z;
	} ar[] = {
		1, 2, 3,
		4, 5, 6
	};
	printf("%d\n", (int)sizeof ar);
	printf("%d %d %d\n", ar[0].x, ar[0].y, ar[0].z);
	printf("%d %d %d\n", ar[1].x, ar[1].y, ar[1].z);
	return 0;
}

