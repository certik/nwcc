#include <stdio.h>

int
main() {
	/* Check with more than one storage unit, and whether
	 * mixing with ordinary char at end works and still
	 * aligns properly
	 */
	struct foo {
		unsigned	a:3;
		unsigned	b:4;	/* 7 */
		unsigned	c:3;	/* 12 */
		unsigned	d:17;	/* 29 */
		/* unit boundary on x86 */
		unsigned	f:31;	/* 32-63 */
		unsigned	g:1;	/* 64 */
		char		x; /* align me */
	} ;
	struct foo	fa[] = {
		{ 1, 12, 2, 0x462, 0xefefef, 1, 33 }
	};
	int	i;

	printf("%u\n", fa[0].f);
	printf("%u, %u\n", fa[0].f , fa[0].g);
	return 0;
}

