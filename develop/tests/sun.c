#include <stdio.h>
#include <limits.h>


struct bogus {
	char	buf[1000000];
};

union nonsense {
	int	x;
	long 	b;
	char	*f;
	double	d;
	long double ld;
};

void
func(int x, int y, long l, struct bogus hahahah, int cont, union nonsense non, float dummy,
   int sp)
{
//sync();
	printf("%d %d %ld %d %f %d\n",
		x, y, l, cont, dummy, sp);
}

int
main() {

	struct bogus b;
	union nonsense n;
	func(1, 2, 3l, b, 4, n, 5.0f, 6);
}

