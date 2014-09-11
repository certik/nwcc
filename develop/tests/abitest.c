#include <stdio.h>
#include <stdarg.h>
#include <limits.h>

char
for_char(char arg) {
	char	x = arg;
	printf("%d\n", x);
	return x;
}

short
for_short(short arg) {
	short	x = arg;
	printf("%d\n", x);
	return x;
}
int
for_int(int arg) {
	int	x = arg;
	printf("%d\n", x);
	return x;
}
long
for_long(long arg) {
	long	x = arg;
	printf("%ld\n", x);
	return x;
}
#if 0
long long
for_llong(long long arg) {
	long long	x = arg;
	printf("%d\n", x);
}
#endif
#if 0 
float
for_float(float arg) {
	float	x = arg;
	printf("%f\n", x);
	return x;
}
double
for_double(double arg) {
	double	x = arg;
	printf("%f\n", x);
	return x;
}
long double
for_ldouble(long double arg) {
	long double	x = arg;
	printf("%Lf\n", x);
	return x;
}
#endif
struct foo {
	int	x;
	char	*p;
} for_struct(struct foo arg) {
	struct foo	x = arg;
	printf("%d,%s\n", x.x, x.p);
	return x;
}

union bar {
	int	x;
	char	*p;
} for_union(union bar arg) {
	union bar	x = arg;
	printf("%d\n", x.x);
	return x;
}

void
variadic(char *fmt, ...) {
	va_list	va;
	char	*p;

	va_start(va, fmt);
	for (p = fmt; *p != 0; ++p) {
		if (*p == '%') {
			if (*++p == 's') {
				char	*s = va_arg(va, char *);
				printf(" %s ", s);
			} else if (0) {
				/* ... */
			}
		}
	}
}


int
main(void) {
	char		c = CHAR_MAX;
	short		s = SHRT_MAX;
	int		i = INT_MAX;
	long		l = LONG_MAX;
	float		flt = 123.4f;;
	double		dbl = 456.0;
	long double	ldbl = 128.87;
	struct foo	f = { 456, "hello" };
	union bar	b;

	b.x = 77777;
	printf("%d\n", for_char(c));
	printf("%d\n", for_short(s));
	printf("%d\n", for_int(i));
	printf("%ld\n", for_long(l));
#if 0
	printf("%f\n", for_float(flt));
	printf("%f\n", for_double(dbl));
	printf("%Lf\n", for_ldouble(ldbl));
#endif

	f = for_struct(f);
	printf("%d,%s\n", f.x, f.p);
	b = for_union(b);
	printf("%d\n", b.x);
	return 0;
}	
