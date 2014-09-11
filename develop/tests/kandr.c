#include <stdio.h>

void
foo(lol)
{
	printf("%d\n", lol);
}

void
bar(x, lol, y)
	int	x, y;
{
	printf("%d, %d, %d\n", x, lol, y);
}

void
baz(x, y)
	int	y, x;
{
	printf("%d, %d\n", x, y);
}	

int
main() {
	foo(123);
	bar(1, 2, 3);
	baz(123, 456);
}

