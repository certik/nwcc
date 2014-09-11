#include <stdio.h>

int
main(void) {
	int	x;
	x = 1? 0: 1;
	printf("%d\n", x);
	x = getchar() != EOF? 5 - 1: +16;
	printf("%d\n", x);
}

