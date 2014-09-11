#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

struct tiny {
	char c;
	char d;
};

/*
 * Check that small structs are passed correctly (aligned to dword
 * boundary on both sides) via stdarg
 */
void
f(int n, ...)
{
	struct tiny 	x;
	long		l;
	va_list 	ap;

	puts("===\nf()");

	va_start (ap,n);

	x = va_arg (ap,struct tiny);
	printf("%d, %d\n", x.c, x.d);
	x = va_arg (ap,struct tiny);
	printf("%d, %d\n", x.c, x.d);
	x = va_arg (ap,struct tiny);
	printf("%d, %d\n", x.c, x.d);
	l = va_arg(ap, long);
	printf("%ld\n", l);
	va_end (ap);
}

/*
 * Check that small structs are passed correctly (aligned to dword
 * boundary on both sides)
 */
void
f2(int n, struct tiny x1, struct tiny x2, struct tiny x3, ...)
{
	long		l;
	va_list 	ap;

	puts("===\nf2()");

	va_start (ap,x3);

	printf("%d, %d\n", x1.c, x1.d);
	printf("%d, %d\n", x2.c, x2.d);
	printf("%d, %d\n", x3.c, x3.d);
	l = va_arg(ap, long);
	printf("%ld\n", l);
	va_end (ap);
}

/*
 * Check that small structs are passed correctly (aligned to dword
 * boundary on both sides) even with intervening (and alignment-
 * changing) small types
 */
void
f3(int n, char dummy, struct tiny x1, char dummy2, struct tiny x2, short dummy3, long foo, struct tiny x3, ...)
{
	long		l;
	va_list 	ap;

	puts("===\nf3()");

	va_start (ap,x3);

	printf("%d, %d\n", x1.c, x1.d);
	printf("%d, %d\n", x2.c, x2.d);
	printf("%d, %d\n", x3.c, x3.d);
	l = va_arg(ap, long);
	printf("%ld\n", l);
	printf("%d, %d, %d\n", dummy, dummy2, dummy3);
	printf("%ld\n", foo);
	va_end (ap);
}

/*
 * Check that small structs are passed correctly (aligned to dword
 * boundary on both sides) even with intervening (and alignment-
 * changing) small types
 */
void
f4(int n, ...)
{
	long		l;
	va_list 	ap;
	struct tiny	tmp;

	puts("===\nf4()");

	va_start(ap,n);

	printf("%d\n", va_arg(ap, int)); /* char */
	tmp = va_arg(ap, struct tiny);
	printf("%d, %d\n", tmp.c, tmp.d);
	printf("%ld\n", va_arg(ap, long));
	printf("%f\n", va_arg(ap, double));
	tmp = va_arg(ap, struct tiny);
	printf("%d, %d\n", tmp.c, tmp.d);
	printf("%d\n", va_arg(ap, int)); /* char */
	printf("%d\n", va_arg(ap, int)); /* short */
	printf("%lld\n", va_arg(ap, long long));
	tmp = va_arg(ap, struct tiny);
	printf("%d, %d\n", tmp.c, tmp.d);

	l = va_arg(ap, long);
	va_end (ap);
}

