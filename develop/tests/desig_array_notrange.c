#include <stdio.h>


int	foo[20] = {
#if 0
	[0 ... 5] = 123,
	456,
	789,
	666,
	[8 ... 10] = 012,
	[9] = 777
#endif
	[0] = 123,
	[1] = 123,
	[2] = 123,
	[3] = 123,
	[4] = 123,
	[5] = 123,
	456,
	789,
	666,
	[8] = 012,
	[9] = 012,
	[10] = 012,
	[9] = 777
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

