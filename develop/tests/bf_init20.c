#include <stdio.h>

static int
func() {
	return 123;
}

int
main() {
	/* Check basic variable initializers */
	struct bogus {
		int	x:8;
		char	buf[3];
	} b = {
		func(), 1, 2, 3
	};
	printf("%d\n", (int)sizeof(struct bogus));
	printf("%d %d %d %d\n", b.x, b.buf[0], b.buf[1], b.buf[2]);

	return 0;
}

