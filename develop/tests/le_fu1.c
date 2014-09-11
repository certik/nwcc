#include <stdio.h>

void f(
double a0, char a1, double a2, double a3, short a4, 
double a5, double a6, double a7, long a8, int a9, 
int a10, int a11, double a12, double a13
)
{
	printf("%f\n", a0);
	printf("%d\n", a1);
	printf("%f\n", a2);
	printf("%f\n", a3);
	printf("%d\n", a4);
	printf("%f\n", a5);
	printf("%f\n", a6);
	printf("%f\n", a7);
	printf("%ld\n", a8);
	printf("%d\n", a9);
	printf("%d\n", a10);
	printf("%d\n", a11);
	printf("%f\n", a12);
	printf("%f\n", a13);

}

int main() {
	f(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13);
	return 0;
}
