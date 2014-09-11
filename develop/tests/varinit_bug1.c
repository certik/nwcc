#include <stdio.h>


static int	func1() { return 1; }
static int	func2() { return 2; }
static int	func3() { return 3; }
static int	func6() { return func3()+func2()+func1(); }

int
main() {
	/* Check with more than one storage unit, and whether
	 * mixing with ordinary char at end works and still
	 * aligns properly
	 *
	 * Mix with variable initializers too
	 */
	struct foo {
		char		x; /* align me */
		char		gnu;
		unsigned	d;
	} fa /*[]*/ = {
		func1(), 5, 6
	};
	int	i;



	printf("%d\n", (int)sizeof(struct foo));

	printf("%u\n", fa/*[0]*/.d);
	return 0;
}

