#include <stdio.h>
struct s0 {
	float m0;
};

struct s1 {
	long double m0;
	double m1;
	char m2;
	long m3;
};

#include <stdarg.h>

void f(
int a0, int a1, int a2, int a4,
int a9, 
int a10, ...
)
{
	va_list va;
	#if 0
	#if 1 
//	printf("%d\n", a0);
	printf("%d\n", a1);
	printf("%d\n", a2);
	#endif
	printf("%d\n", a4);
	printf("%d\n", a9);
	printf("%d\n", a10);
	#endif
	va_start(va, a10);
	{
sync();
	double a14 = va_arg(va, double);
sync();
	printf("%f\n", a14);
	}
	va_end(va);

}

int main() {
	f(0, 1, 2,4, 12, 13, (double)17);
	return 0;
}
