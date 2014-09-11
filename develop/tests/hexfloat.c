#include <stdio.h>

int
main(){
	printf("%f\n", 0x1.0p0);
	printf("%f\n", 0x1.p0f);
	printf("%Lf\n", 0x1.p0L);
	printf("%f\n", 0x1.p0);
	printf("%f\n", -0x1.p0);
	printf("%f\n", 0x0.1p0);
	printf("%f\n", 0x1b0.p0);
	printf("%f\n", 0x10.1p0);
	printf("%f\n", 0x1.01p0);
	printf("%f\n", 0x1.001p0);
	printf("%f\n", 0x1.1p1);
	printf("%f\n", 0x1.1p2);
	printf("%f\n", 0x1.1p-1);
	printf("%f\n", 0x1.1p-2);
}
