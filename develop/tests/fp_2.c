#include <stdio.h>

float
getfloat() {
	static float	far[] = {
		3242.22f, -256.32L, 1467, 842U, 531.44f, -11.642, 0
	};
	static int	idx;
	return far[idx++];
}

double
getdouble() {
	static double	lar[] = {
		33.22, -426.22L, -2262, 547U, 241.22f, +333.66, 0
	};
	static int	idx;
	return lar[idx++];
}

long double
getlongdouble() {
	static long double	ldar[] = {
		136.322L, 35252.2, 352, 11LU, -4363.55f, 345214.222L, 0
	};
	static int	idx;
	return ldar[idx++];
}

int
main() {
	float		f;
	double		d;
	long double	ld;

	while ((f = getfloat()) != 0.0f) {
		printf("%f %f %Lf\n", f, getdouble(), getlongdouble());
	}
	return 0;
}


