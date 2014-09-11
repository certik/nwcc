#include <stdio.h>

int
main() {
	/* Check basic variable initializers */
	struct bogus {
		int	:16;
		int	:0;
		int	x:17;
	} b = {
		123
	};
	printf("%d\n", (int)sizeof(struct bogus));
	printf("%d\n", b.x);

	return 0;
}

