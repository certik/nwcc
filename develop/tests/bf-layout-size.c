#include <stdio.h>


int
main() {
	struct foo {
		char	x;
		int	y:3;
	};
	struct foo2 {
		char	x;
		int	y:17;
	};
	struct foo3 {
		char	x;
		int	y:24;
	};
	struct foo4 {
		char	x;
		int	y:24;
	};
	struct foo5 {
		char	x;
		int	y:25;
	};
	struct foo6 {
		char	x[4];
		int	y:1;
	};
	printf("%d\n", (int)sizeof(struct foo));
	printf("%d\n", (int)sizeof(struct foo2));
	printf("%d\n", (int)sizeof(struct foo3));
	printf("%d\n", (int)sizeof(struct foo4));
	printf("%d\n", (int)sizeof(struct foo5));
	printf("%d\n", (int)sizeof(struct foo6));
}

