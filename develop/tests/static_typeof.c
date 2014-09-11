#include <stdio.h>

int *
foo() {
	extern int	bogus;
	static __typeof(bogus)	f;

	++f;
	return &f;
}

int
main() {
	int	i;

	for (i = 0; i < 5; ++i) {
		printf("%d\n", *foo());
	}
	return 0;
}

