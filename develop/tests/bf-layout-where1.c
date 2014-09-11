#include <stdio.h>
#include <string.h>

int
main() {
	struct foo {
		unsigned	x:7;
		unsigned 	y:7;
	} f;
	struct foo2 {
		unsigned	x:7;
		unsigned 	y:7;
	} f2;

	memset(&f, 0, sizeof f);
	f.x = 0x7f;
	printf("%d\n", f.x);

	memset(&f, 0, sizeof f);
	f.y = 0x7f;
	printf("%d\n", f.y);

	memset(&f2, 0, sizeof f2);
	f2.y = 0x7f;
	printf("%d\n", f2.y);
}

