#include <stdio.h>

int
main() {
	struct foo {
		int	x;
		char	buf[8];
	};
	printf("%d\n", (int)sizeof (struct foo){ 0 });
	return 0;
}


