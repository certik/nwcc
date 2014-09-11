
#if 0

#include <stdio.h>
#endif


static void
test_array_init(int arg) {
	int	i_ar[] = {
		123,
		456,
		arg
			,
		4362,
		(arg & 0xff) - 12,
		4444,
		((&arg + 1) - 1)[0] % 14,
		236,
		-1
	};
	int	i;
	for (i = 0; i_ar[i] != -1; ++i) {
		printf("%d  ", i_ar[i]);
	}
	putchar('\n');
}

int
main() {
	test_array_init(0xf22625);
}

