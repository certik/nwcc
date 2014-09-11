#include <stdio.h>

void
f(long double d0, long double d1, long double d2, long double d3, long double d4, long double d5, long double d6)
{
	printf("%Lf\n", d0);
	printf("%Lf\n", d1);
	printf("%Lf\n", d2);
	printf("%Lf\n", d3);
	printf("%Lf\n", d4);
	printf("%Lf\n", d5);
	printf("%Lf\n", d6);
}

int
main() {
	f(1.0L, 2.0L, 3.0L, 4.0L, 5.0L, 6.0L, 7.0L);
}
