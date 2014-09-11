#include <stdio.h>


int
main() {
	struct foo {
		char	x;
		int	:0;
	};
	printf("%d\n", (int)sizeof(struct foo));
}

