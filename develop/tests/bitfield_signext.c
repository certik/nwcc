#include <stdio.h>

int
main() {
	int	n = 123;
	struct foo {
		unsigned int	x:4;
	} f, f2 = { 123 }, f3 = { n };
	struct bar {
		int	x:6;
	} b;
	struct baz {
		long long	gnu:10;
	} baz;
	f.x =  123;
	printf("%d\n", f.x);
	printf("%d\n", f2.x);
	printf("%d\n", f3.x);
	b.x = -3;
	printf("%d\n", (b.x << 26) >> 26); 
	printf("%d\n", b.x);

	baz.gnu = (-46);
	printf("%d\n", baz.gnu);
	return 0;
}

