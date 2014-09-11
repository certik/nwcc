#include <stdio.h>
#include <limits.h>

void
test0(float a0 , double a1) {
	printf("%f\n", a0);
	printf("%f\n", a1);
}


int
main() {
	test0(1.1, 2.2);
	return 0;
}

