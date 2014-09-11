#include <stdio.h>
#include <stdlib.h>


int
main() {
	int	i;
	int	j;

	srand(12345);
	for (i = 0; i < 25; ++i) {
		for (j = 0; j < 5; ++j) {
			double 		d = rand() / (double)rand();
			long double	ld = d;

			printf("%f=%Lf ", d, ld);
		}
		putchar('\n');
	}
	for (i = 0; i < 25; ++i) {
		for (j = 0; j < 5; ++j) {
			long double 	ld = rand() / (long double)rand();
			double		d = ld;

			printf("%Lf=%f ", ld, d);
		}
		putchar('\n');
	}
	return 0;
}

