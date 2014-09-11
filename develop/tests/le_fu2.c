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
int a0, int a1, char a2, struct s0 a3, short a4, 
long double a5, struct s1 a6, float a7, long a8, int a9, 
int a10, long double a11, float a12, ...
)
{
	va_list va;
	#if 1 
	printf("%d\n", a0);
	printf("%d\n", a1);
	printf("%d\n", a2);
	#endif
	printf("%f\n", a3.m0);
	printf("%d\n", a4);
	printf("%Lf\n", a5);
	printf("%Lf\n", a6.m0);
	printf("%f\n", a6.m1);
	printf("%d\n", a6.m2);
	printf("%ld\n", a6.m3);
	printf("%f\n", a7);
	printf("%ld\n", a8);
	printf("%d\n", a9);
	printf("%d\n", a10);
	printf("%Lf\n", a11);
	printf("%f\n", a12);
	va_start(va, a12);
	{
	int a13 = va_arg(va, int);
	printf("%d\n", a13);
	}
	{
	float a14 = va_arg(va, double);
	printf("%f\n", a14);
	}
	{
	char a15 = va_arg(va, int);
	printf("%d\n", a15);
	}
	{
	float a16 = va_arg(va, double);
	printf("%f\n", a16);
	}
	{
	double a17 = va_arg(va, double);
	printf("%f\n", a17);
	}
	va_end(va);

}

int main() {
	f((int)0, (int)1, (char)2, (struct s0){(float)3}, (short)4, (long double)5, (struct s1){(long double)6, (double)7, (char)8, (long)9}, (float)10, (long)11,(int)12, (int)13, (long double)14, (float)15, (int)16, (float)17, (char)18, (float)19, (double)20);
	return 0;
}
