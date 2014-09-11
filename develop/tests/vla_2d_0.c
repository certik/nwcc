#include <stdio.h>

int
main() {
	int	x = 12;
	int	y = 45;
	char	buf[x][y];

	printf("%d\n", sizeof buf);
	printf("%d\n", sizeof *buf);
	printf("%d\n", sizeof buf[0]);
	return 0;
}

