#include <stdio.h>

int
main() {
	char	x = 123;
	short	y = 1203;
	int	z = 132213;
	long	a = 123000000;
	long long	b = 10000000;
	float	f;
	double	d;
	long double	ld;

#define DOFLOAT(x) f=x; printf("%f\n", f)
#define DODOUBLE(x) f=x; printf("%f\n", f)
#define DOLDOUBLE(x) f=x; printf("%f\n", f)
	DOFLOAT(x);
	DOFLOAT(y);
	DOFLOAT(z);
	DOFLOAT(a);
	DOFLOAT(b);

	DODOUBLE(x);
	DODOUBLE(y);
	DODOUBLE(z);
	DODOUBLE(a);
	DODOUBLE(b);

	DOLDOUBLE(x);
	DOLDOUBLE(y);
	DOLDOUBLE(z);
	DOLDOUBLE(a);
	DOLDOUBLE(b);
	return 0;
}	
	


