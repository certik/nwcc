#include <stdio.h>

int
main() {
	long double ld  = 123.566L;
	printf("%Lf %Lf\n", ld, 353.33L);
printf("%Lf\n", ld);
	printf("%Lf %d %Lf\n", ld, 456, 353.33L);
printf("%d %Lf\n", 1, 123.566L);
	return 0;
}
