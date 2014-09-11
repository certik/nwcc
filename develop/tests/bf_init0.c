#include <stdio.h>

int
main() {
	struct foo {
		unsigned	x:1;
	} f = { 1 };
	printf("%d\n", f.x);
	return 0;
}

