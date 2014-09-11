#include <stdio.h>

int
main(void) {
	int	x;

	x = 1 > 2;
	printf("1 > 2 is %d\n", x);
	x = 1 < 2;
	printf("1 < 2 is %d\n", x);
	x = 2 >= 2;
	printf("2 >= 2 is %d\n", x);
	x = 3 <= 2;
	printf("3 <= 2 is %d\n", x);
	x = 5 == 5;
	printf("5 == 5 is %d\n", x);
	x = 5 != 5;
	printf("5 != 5 is %d\n", x);

	return 0;
}

