#include <stdio.h>
#include <stdarg.h>

void
foo(const char *fmt, ...) {
	va_list	va;
	int	i;

	va_start(va, fmt);
	while ((i = va_arg(va, int)) != 0) {
		printf("%d\n", i);
	}

	va_end(va);
}

int
main() {
	foo("hello", 1, 2, 3, 4, 5, 11, 0);
}
