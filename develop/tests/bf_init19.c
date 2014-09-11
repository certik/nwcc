#include <stdio.h>

int
main() {
	/* Check terminator at end */
	struct bogus {
		int	:16;
		int	x:17;
		int	:0;
	} b = {
		123
	};
	printf("%d\n", (int)sizeof(struct bogus));
	printf("%d\n", b.x);

	return 0;
}

