#include <stdio.h>

int
main() {
	float	foo = 0.0f;
	double	bar = 1.4;
	long double baz = 0.0L;

	if (!foo) {
		puts("good");
	}
	if (bar) {
		puts("good too");
	}
	if (!baz) {
		puts("perfect!");
	}	
}

