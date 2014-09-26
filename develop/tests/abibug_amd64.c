#include <stdio.h>
struct s0 {
	int m0;
	char m1;
	float m2;
};

struct s1 {
	double m0;
	int m1;
	int m2;
	double m3;
};

#include <stdarg.h>

void f(
char a0, long long a1, long a2, struct s0 a3, int a4, 
int a5, double a6, long long a7, double a8, char a9, 
struct s1 a10, double a11, ...
)
{
	va_list va;
	printf("%d\n", a0);
	printf("%lld\n", a1);
	printf("%ld\n", a2);
	printf("%d\n", a3.m0);
	printf("%d\n", a3.m1);
	printf("%f\n", a3.m2);
	printf("%d\n", a4);
	printf("%d\n", a5);
	printf("%f\n", a6);
	printf("%lld\n", a7);
	printf("%f\n", a8);
	printf("%d\n", a9);
	printf("%f\n", a10.m0);
	printf("%d\n", a10.m1);
	printf("%d\n", a10.m2);
	printf("%f\n", a10.m3);
	printf("%f\n", a11);
	va_start(va, a11);
	{
	int a12 = va_arg(va, int);
	printf("%d\n", a12);
	}
	{
	char a13 = va_arg(va, int);
	printf("%d\n", a13);
	}
	{
	int a14 = va_arg(va, int);
	printf("%d\n", a14);
	}
	va_end(va);

}

int main() {
	f((char)0, (long long)1, (long)2, (struct s0){(int)3, (char)4, (float)5}, (int)6, (int)7, (double)8, (long long)9, (double)10, (char)11, (struct s1){(double)12, (int)13, (int)14, (double)15}, (double)16, (int)17, (char)18, (int)19);
	return 0;
}
