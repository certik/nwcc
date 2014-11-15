#include <stdarg.h>
#include <stdio.h>

void
printint(int x, ...) {
	va_list	va;
	int	arg;

	va_start(va, x);
	arg = 1? va_arg(va, int) : va_arg(va, double);
	printf("%d %d\n", x, arg);
}

void
printdouble(int x, ...) {
	va_list	va;
	double	arg;
	va_start(va, x);
	arg = 0? va_arg(va, int): va_arg(va, double);
	printf("%d %f\n", x, arg);
}

int
main() {
	printint(123, 555);
	printdouble(456, 888.0);
}
	

