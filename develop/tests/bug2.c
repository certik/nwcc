#include <stdio.h>

int
main() {
	char	x = 11;
	short	y = 7;

	printf("%d\n", sizeof (x = 3));
	printf("%d\n", sizeof (y = 66));
	printf("%d %d\n", sizeof (x += 17), sizeof (y -= 8));
	printf("%d %d\n", x, y);
}

