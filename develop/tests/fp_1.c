#include <stdio.h>

static long double
stuff(double arg) {
	return arg;
}	

int
main() {
	float		ar[] = { 123.44, 25.4f, 444.2L };
	float		*fp;
	long double	ld;

	for (fp = ar; fp < ar + sizeof ar / sizeof ar[0]; ++fp) {
		double	d = *fp * 1.4L / 16.3f;
		printf("%f\n", *fp);
		printf("%f\n", d);
		if ((int)d % 2) {
			puts("odd");
		} else {
			puts("even");
		}
		(void) stuff(d);
		ld = stuff((float)d);
		printf("%Lf\n", ld);
	}
	return 0;
}	
	
