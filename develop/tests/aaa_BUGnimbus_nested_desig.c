#include <stdio.h>

int
main() {
	struct {
		int	x;
		struct {
			int	y, z;
		} nested;
		int	y;
		char	buf[128];
	} s = {
		.nested.y = 123, 456,
		.x = 7774, .y = 2222, "hello"
	};

	printf("%d   %d,%d     %d   %s\n",
		s.x, s.nested.y, s.nested.z, s.y, s.buf);
	return 0;
}

