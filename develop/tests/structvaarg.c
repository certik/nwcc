#include <stdio.h>
#include <stdarg.h>

struct bogus {
	int	x;
	char	*y;
	int	z;
};	


void
foo(char *dummy, ...) {
	va_list	va;
	struct bogus	b;
	va_start(va, dummy);
	
	b = va_arg(va, struct bogus);
	printf("%d, %s, %d\n", b.x, b.y, b.z);
}	

int
main() {
	struct bogus	b;

	b.x = 123;
	b.y = "hello";
	b.z = 456;
	foo(NULL, b);
}

