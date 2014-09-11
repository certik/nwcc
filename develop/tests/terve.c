#include <stdio.h>
#include <stdarg.h>


struct k {
	void	*p;
	int	x;
	int	y;
};

void
foo(void (*func)(), struct k k, const char *fmt, ...) {
	va_list	va;
	va_start(va, fmt);
	printf("%s 1\n", va_arg(va, char *));
	printf("%s 2\n", va_arg(va, char *));
	printf("%s 3\n", va_arg(va, char *));
	printf("%s 4\n", va_arg(va, char *));
	printf("%s 5\n", va_arg(va, char *));
	printf("%s 6\n", va_arg(va, char *));
	va_end(va);
}

int
main() {
	struct k k;
	foo(NULL, k, "%s %s %s %s %s %s", "a", "b", "c", "d", "e", "f");
}

