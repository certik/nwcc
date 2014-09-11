#include <stdio.h>

int
main() {
	int x = 0? 1, 0 : 0;
	int y = 1? 1, 0 : 0;
	int z = 0? 0, 1 : 0;
	int a = 1? 0, 1 : 0;

	printf("%d\n", x);
	printf("%d\n", y);
	printf("%d\n", z);
	printf("%d\n", a);
}
