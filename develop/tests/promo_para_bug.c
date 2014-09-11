#include <stdarg.h>
#include <stdio.h>

/*
 * 07/21/08: Offsets for stack char/short parameters were messed up, so
 * stdarg stuff didn't work.
 */
void
bar(char d, ...) {
	unsigned long long x;
	va_list v;
	va_start(v, d);
	x = va_arg(v, unsigned long long);
	printf("%d, %lld\n", d, x);
	va_end(v);
}

void
bar2(short d, ...) {
	unsigned long long x;
	va_list v;
	va_start(v, d);
	x = va_arg(v, unsigned long long);
	printf("%d, %lld\n", d, x);

	va_end(v);
}

void
bar3(char x, short y, int i, char f, char d, ...) {
	unsigned long long lx;
	va_list v;
	va_start(v, d);
	lx = va_arg(v, unsigned long long);
	printf("%d, %lld\n", d, lx);
	printf("   [%d, %d, %d, %d]\n", x, y, i, f);
	va_end(v);
}
void
bar4(short s, short s2, short s3, char d, ...) {
	unsigned long long x;
	va_list v;
	va_start(v, d);
	x = va_arg(v, unsigned long long);
	printf("%d, %lld\n", d, x);
	printf("   [%d, %d, %d]\n", s, s2, s3);
	va_end(v);
}

int
main(void) {
	bar(0, 16LL);
	bar2(1, 32LL);
	bar3(33, 22, 11, 44, 2, 600000000000000LL);
	bar4(777, 666, 555, 3, 0x1ffffffffffffLL);
	exit(0);
}

