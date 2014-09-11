#include <stdarg.h>
#include <stdio.h>

typedef struct { char x[0]; } A0;
typedef struct { char x[1]; } A1;
typedef struct { char x[2]; } A2;


void
foo(int gnu, A0 a0, A1 a1, A2 a2) {
	va_list	va;

	printf("%d\n", gnu);
	printf("%c\n", a1.x[0]);
	printf("%c %c\n", a2.x[0], a2.x[1]);
}

/*
 * 05/22/11: Passing an empty struct (GNU C) to a function caused a crash
 * in the gcc test suite on AMD64 (this probably affected all other targets
 * as well, but may not have bombed so far)
 */
int
main(void) {
	A0	a0; 
	A1	a1 = { 'x' };
	A2	a2 = { "zy" };

  	foo(21, a0, a1, a2);
	return 0;
}

