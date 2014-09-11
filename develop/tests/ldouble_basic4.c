#include <stdio.h>

long double
foo(long double arg) {
	printf(" gnu  %Lf\n", arg);
	return arg;
}

int
main() {
	long double	d;
	d = foo(555.3333L);
	printf("%Lf\n", d);
	return 0;
}

