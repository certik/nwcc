#include <stdio.h>


struct gnu lol2;
double d = 123.34556;
int
main() {
	int	passed = 0;
	struct gnu {
		int	x;
	};

	struct gnu lol = { 12 };
	printf("%d\n", lol.x);

again:
	lol.x = 456;
	lol2 = lol;
	printf("%d\n", lol2.x);
	printf("%f\n", -d);


	if (!passed) {
		passed = 1;
		goto again;
	}
}

