#include <stdio.h>

int
main() {
	char	x;
	short	y;

	/*
	 * Really unnecessary bugs: Missing promotion for operands
	 * of ~ and - operators
	 */
	printf("%d\n", sizeof ~x);
	printf("%d\n", sizeof ~y);
	printf("%d\n", sizeof -x);
	printf("%d\n", sizeof -y);
	printf("%d\n", sizeof +x);
	printf("%d\n", sizeof +y);
	printf("%d\n", sizeof !x);
	printf("%d\n", sizeof !y);
	printf("%d\n", sizeof --x);
	printf("%d\n", sizeof --y);
	printf("%d\n", sizeof x++);
	printf("%d\n", sizeof y++);
	return 0;
}

