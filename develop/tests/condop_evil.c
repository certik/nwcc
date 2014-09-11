#include <stdio.h>

int
main() {
	struct foo {
		int		x;
		char	*p;
	} a, b, c;
	int	i;

	b.x = 123, c.x = 456;
	b.p = "hello", c.p = "world";

	for (i = 0; i < 5; ++i) {
		a = i % 2? b: c;
		printf("%d,%s\n", a.x, a.p);
	}
	return 0;
}

