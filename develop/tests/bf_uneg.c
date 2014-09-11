#include <stdio.h>


int
main() {
	struct foo {
		int x:30;
		int y:2;
	} f;

	f.x = 123;
	f.y = 0;
	printf("%d\n", -f.x );
	printf("%d\n", -f.y);
	f.y = 1;
	printf("%d\n", +f.x );
	printf("%u\n", +f.y);
}

