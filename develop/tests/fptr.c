#include <stdio.h>

int
main(void) {
	int	(*f)(int ch);
	f = putchar;
	f('x');
	(*f)('x');
	(******f)("hehe"[2]);
	f = &putchar;
	((((***f))))('y');
	printf("hmlol\n");
}

