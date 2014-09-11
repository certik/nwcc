#include <stdio.h>

struct foo {
	unsigned	x:1;
} f;


struct foo
func() {
	struct foo	f;
	return f;
}

int
main() {

	/*
	 * Taking the size should be legal, but implementation-defined. Use
	 * !! to avoid that the test suite reports BAD CODE (multiple compilers
	 * yield different results, e.g. some versions of gcc 1, other versions
	 * and tinycc/tendra 4)
	 */
	printf("%d\n", (int)!!sizeof(f.x = 1));
	printf("%d\n", (int)!!sizeof(0, f.x));
	printf("%d\n", (int)sizeof(0, (char)0)); /* Make sure nothing is promoted */
	printf("%d\n", (int)sizeof(0? f.x: f.x)); /* Is promoted! */
	printf("%d\n", (int)sizeof(0? (char)0: (char)0)); /* Is promoted! */
/*	printf("%d\n", (int)sizeof func().x);  not allowed?! */
	return 0;
}

