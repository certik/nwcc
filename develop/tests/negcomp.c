#include <stdio.h>

int
foo() {
	return -128;
}

int
main() {
	int	x;

	if (-128 != (x = foo())) {
		puts("impossible");
	} else {
		printf(" ... %d !!! ...\n", x);
	}
	return 0;
}	
		
