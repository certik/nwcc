#include <stdio.h>

int
main(void) {
	float	x;
	float	y;
	float	z;
	int	i;

	x = 123.656;
	y = 12.1f;
	z = x * y;
	printf("%f\n", z);

	y = 12;
	z = x * y;
	printf("%f\n", z);
	i = z;
	printf("... and without fractional part: %d\n", i);
#if 0
	printf("... and without fractional part: %d\n", (iny)z);
#endif
	z =z ;
}

