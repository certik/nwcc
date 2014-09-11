#include <stdio.h>

union foo {
	int	x;
	char	ar[4];
};

void
foo(int lol, union foo p1, int hehe) {
	printf("x = %d\n", p1.x);
	printf("ar[0 - 3] = %d,%d,%d,%d\n",
		p1.ar[0], p1.ar[1], p1.ar[2], p1.ar[3]);	
	printf("%d %d\n", lol, hehe);
}

int
main(void) {
	union foo	f;

	f.x = 123;
	foo(999, f, 777);
	return 0;
}

