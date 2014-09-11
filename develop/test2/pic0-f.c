#include <stdio.h>

int	lib_var;

int
bogus_stuff() {
	return lib_var;
}

void
lib_func0() {
	puts("hello");
	printf("%d\n", lib_var);
}

void
lib_func1(int val) {
	printf("world  %d\n", val);
}

void
lib_func2() {
	int	i = 100;

	sync();
	for (i = 0; i < 5; ++i) {
		int	x;

		puts("kekeke");
		x = lib_var * i / 2.1f;
		
		printf("%d\n", x);
	}
	printf("%d\n", 0);
}

