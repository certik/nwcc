#include <stdio.h>

long double
foo() {
	return 123.567L;
}

int
main() {
	long double	d;
	d = foo();
	printf("%Lf\n", d);
	return 0;
}

