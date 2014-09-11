#include <stdio.h>

int
main() {
#if defined __NWCC__
	for (int i = 0; i < 5; ++i) {
		puts("hello");
	}	
	int	i = 3;
	printf("%d\n", i);

	int temp, j, k;
	for (int temp = 0; temp < 3; ++temp)
		for (int j = 1, k = 0; j < 6; ++j, k += 2)
			if (j % 2)
				do
					printf("k=%d\n", k);
				while (0);
			else {
				for (i = 0; i < 1; ++i)
					printf("j*k=%d\n", j*k);
			}

#else
puts("hello");
puts("hello");
puts("hello");
puts("hello");
puts("hello");
puts("3");
puts("k=0");
puts("j*k=4");
puts("k=4");
puts("j*k=24");
puts("k=8");
puts("k=0");
puts("j*k=4");
puts("k=4");
puts("j*k=24");
puts("k=8");
puts("k=0");
puts("j*k=4");
puts("k=4");
puts("j*k=24");
puts("k=8");
#endif
	return 0;
}

