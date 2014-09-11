#include <stdio.h>

int
main() {
	int	x;
	int	i;
	for (i = 1; i < 5; ++i) {
		char	ar[i];
		printf("%d\n", sizeof ar);
	}
}

