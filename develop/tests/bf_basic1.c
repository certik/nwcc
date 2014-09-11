#include <stdio.h>
#include <string.h>

int
main() {
	struct foo {
		char	dummy[3];
		int	gnu:4;
		int	empty:4;
	} f;
	f.dummy[0] = 1;
	f.dummy[1] = 2;
	f.dummy[2] = 3;

	f.empty = 4;
	f.gnu = 5;
	printf("%d %d %d\n", f.dummy[0], f.dummy[1], f.dummy[2]);
	printf("%d\n", f.empty);
	printf("%d\n", f.gnu);
	return 0;
}

