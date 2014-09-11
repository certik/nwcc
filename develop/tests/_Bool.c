#include <stdio.h>

int
main() {
	_Bool	b = 1;
	printf("%d\n", b);
	++b;
	printf("%d\n", b);
	--b;
	printf("%d\n", b);
	--b;
	printf("%d\n", b);
	--b;
	printf("%d\n", b);
	b = "hello";
	printf("%d\n", b);
}
