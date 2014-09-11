#include <stdio.h>


int	foo[20] = {
	[0 ... 5] = 123,
	456,
	789,
	666,
	[8 ... 10] = 012,
	[9] = 777,
	[3 * 7 - 6 /*15*/ ... -8 + 20 - (-5) /*17*/] = 333

};

int
main() {
	int	i;

	for (i = 0; i < sizeof foo / sizeof foo[0]; ++i) {
		printf("%d", foo[i]);
		if ((i + 1) % 10 == 0) {
			putchar('\n');
		} else {
			printf("  ");
		}	
	}
	putchar('\n');
	return 0;
}

