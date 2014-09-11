#include <stdio.h>


int	foo;

int
main() {
	printf("%d\n", foo);
	return 0;
}

extern int	foo = 123;

int foo;

