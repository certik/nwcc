#include <stdio.h>

int
main() {
	double		d = 123.43;
	float		f = 456.7f;
	long double	ld = 89.1L;

	f = -f;
	d = -d;
	ld = -ld;
	printf("%f\n", f);
	printf("%f\n", d);
	printf("%Lf\n", ld);
	return 0;
}

