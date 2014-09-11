#include <stdio.h>


int
main() {
	struct foo {
		unsigned x:30;
		unsigned y:2;
	} f;

	f.x = 123;
	f.y = 0;
	printf("%u\n", ~f.x );
	printf("%u\n", ~f.y);
	f.y = 1;
	printf("%u\n", ~f.x );
	printf("%u\n", ~f.y);
}

