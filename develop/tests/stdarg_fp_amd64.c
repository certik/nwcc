#include <stdio.h>
#include <stdarg.h>


/*
 * 09/20/2014: This test case exposed a bug in nwcc 0.8.2 (and earlier) on AMD64: Floating
 * point arguments weren't passed correctly if all argument GPRs were used up
 */
void f(
	int a0, int a1, int a2, int a4,
	int a9, 
	int a10, ...
)
{
	va_list va;
	va_start(va, a10);
	{
	double a14 = va_arg(va, double);
	printf("%f\n", a14); /* This would print 0.0 in buggy versions */
	}
	va_end(va);

}

int main() {
	double d = 17;
	printf("%f\n", d);
	f(0, 1, 2,4, 12, 13, d);
	return 0;
}
