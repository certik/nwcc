#include <stdio.h>


/*
 * 11/05/20: Support nonsensical but workable (and sometimes used)
 * construct of shoving a null pointer constant into a smaller
 * integral type in a constant expression
 */
int
main() {
	static int gnu = (char)((void *)0);
	printf("%d\n", gnu);
	return 0;
}

