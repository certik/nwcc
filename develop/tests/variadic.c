#include <stdio.h>
#include <stdarg.h>


void
test1(char *fmt, ...) {
	char	*p;
	va_list	va;

	va_start(va, fmt);

	for (p = fmt; *p != 0; ++p) {
		switch (*p) {
		case 'i': {
			int	i = va_arg(va, int);
			printf("%d\n", i);
			break;
			}
		case 'l': {
			long long	l = va_arg(va, long long);
			printf("%lld\n", l);
			break;
			}
		case 'd': {
			double	d = va_arg(va, double);
			printf("%f\n", d);
			break;
			}
		case 'L': {
			long double	ld = va_arg(va, long double);
			printf("%Lf\n", ld);
			break;
			}
		default:
			;
		}
	}
	va_end(va);
}

int
main() {
	test1("i i d l L i l d L i L i i L",
		1, 2, 3.0, 4LL, 5.0L,
		6, 7LL, 8.0f, 9.0L, 10,
		11.0L, 12, 13, 14.3L);
}

