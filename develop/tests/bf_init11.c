#include <stdio.h>

int
main() {
	struct foo {
		char		gnu;
		unsigned	d:17;
	} f = { 66, 123  };

	printf("%u\n", f.d);
	printf("%d\n", f.gnu);
	return 0;
}

