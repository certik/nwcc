#include <stdio.h>

int
main() {
	char	c;
	char	ar[128];
	struct foo {
		int	x;
	} f = { 123 };

	printf("%d\n", (int)sizeof (0, c));
	printf("%d\n", (int)sizeof (0, ar));
	printf("%d\n", (int)sizeof (0, main));
	printf("%d\n", (0, f).x);
/*	printf("%d\n", &(0, f).x);  not allowed! */
/*	printf("%d\n", &(0? f: f).x);  not allowed! */
/*	printf("%d\n", &func().x);  not allowed! */
	return 0;
}

