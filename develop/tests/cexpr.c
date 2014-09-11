#include <stdio.h>
#include <limits.h>

int
main() {
	/* XXX need to add much more stuff ... */
	static int	x0 = 123 + 456l;
	static int	x1 = (unsigned char)INT_MAX + 17;
	static int	x2 = 888 < 1000 - 111;
	static int	x3 = 4711 * 3 / 18 % 5; 
	static int	x4 = 0? 1 / 0: 8 / 3;

	printf("%d\n", x0);
	printf("%d\n", x1);
	printf("%d\n", x2);
	printf("%d\n", x3);
	printf("%d\n", x4);
}


