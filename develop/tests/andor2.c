#include <stdio.h>

int
main(void) {
	int	x;

	x = 123 ^ 889 - 15;
	if (x == 1337 || getchar() != EOF) {
		puts("somewhat successful");
	}
	return 0;
}	
	
