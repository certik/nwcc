#include <stdio.h>

int
main() {
	/* Check with more than one storage unit, and whether
	 * mixing with ordinary char at end works and still
	 * aligns properly
	 */
	static struct foo {
		unsigned	f:31;
	} f = { 0xefefef }; 

	printf("%u\n", f.f);
	return 0;
}

