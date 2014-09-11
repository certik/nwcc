#include <stdio.h>
#include <limits.h>

void
test0(float a0 , double a1, float a2, double a3, double a4, double a5, double a6, double a7,
double a8, double a9, double a10, double a11, double a12, double a13, double a14, double a15, double a16) {
printf("%f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f\n",
	a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16);
}


int
main() {
	test0(1.1, 2.2, 3.3, 4.4, 5.5, 6.6, 7.7, 8.8, 9.9, 10.10, 11.11, 12.12, 13.13, 14.14, 15.15, 16.16, 17.17);
	return 0;
}

