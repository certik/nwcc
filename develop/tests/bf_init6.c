#include <stdio.h>

int
main() {
	/* Check with more than one storage unit, and whether
	 * mixing with ordinary char at end works and still
	 * aligns properly
	 */
	struct foo {
		unsigned	f:31;
	} fa[1] = { { 0xefefef } }; 
	int	i;
	struct foo  fa2[1] = { 0xefefef }; 

	printf("%u\n", fa[0].f);
	printf("%u\n", fa2[0].f);

	return 0;
}

