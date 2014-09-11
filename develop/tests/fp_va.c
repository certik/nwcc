#include <stdio.h>
#include <stdarg.h>

static void
foo(const char *fmt, ...) {
	va_list	va;
	char	*p;
	va_start(va, fmt);

	for (p = (char *)fmt; *p != 0; ++p) {
		if (*p == 'f') {
			double d = va_arg(va, double);
			printf("%f ", d);
		} else if (*p == 'd') {
			double	d = va_arg(va, double);
			printf("%f ", d);
		} else if (*p == 'i') {
			int	i = va_arg(va, int);
			printf("%d ", i);
		} else if (*p == 'l') {
			long	l = va_arg(va, long);
			printf("%ld\n", l);
		} else if (*p == 'L') {
			long double	ld = va_arg(va, long double);
			printf("%Lf\n", ld);
		} else {
			putchar(*p);
		}
	}

	va_end(va);
}


int
main() {
	foo("i f f d i   l L f l L    i d f L l l",
		128, 133.322f, 432.32f, 53.666, -5822,
		34212L, 5422.777433L, -4572.22f, 77l, 21.7L,
		3217, 2216.4123, 25.3f, -422.2L, 444l,
		11l);
}

