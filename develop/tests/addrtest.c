#include <stdio.h>
#include <limits.h>

int
main(void) {
	int		x;
	static int	y;
	int		*p;

	x = rand();
	y = rand();
	p = &y;

	printf("%d\n", x);
	printf("%d\n", y);
	printf("%d\n", *p);
	printf("%d\n", INT_MAX);
}

