#include <stdio.h>

int
main() {
	char	x __attribute__((aligned(4)));
	char	y __attribute__((aligned)); /* max alignment */
	long	z __attribute__((aligned(2))); /* down-align */

	printf("%d\n", (int)__alignof x); 
	printf("%d\n", (int)__alignof y); 
	printf("%d\n", (int)__alignof z); 

	/* standard alignment */
	printf("%d\n", (int)__alignof(char));
	printf("%d\n", (int)__alignof(short));
	printf("%d\n", (int)__alignof(int));
	printf("%d\n", (int)__alignof(long));
	printf("%d\n", (int)__alignof(long long));
	printf("%d\n", (int)__alignof(float));
	printf("%d\n", (int)__alignof(double));
	printf("%d\n", (int)__alignof(long double));
	printf("%d\n", (int)__alignof(void *));
	printf("%d\n", (int)__alignof(void (*)(void)));
	return 0;
}

