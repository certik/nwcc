#include <stdio.h>

/* 07/26/12: This reveals an AMD64 ABI bug: "bug" and "bug2" were placed incorrectly (bug
* was directly followed by bug2 (bug being 8-byte and bug2 being 16-byte aligned) and the
* required padding space was not allocated
*/

void
foo(double d0, double d1, double d2, double d3, double d4, double d5, double d6, double d7,
	double bug, long double bug2)
{
	printf("%Lf\n", bug2);
	printf("%f\n", bug);
}


int
main() {
	foo(0,1,2,3,4,5,6,7,   8.3L, 9.4);
}

