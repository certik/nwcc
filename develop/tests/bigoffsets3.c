#include <stdio.h>


struct foo {
	char	buf[1000000];
	int	x;
};

int
main() {
	char	buf[100000];
	struct foo	f = { { 0 }, 123 };
	int		i;

	i = f.x;
	printf("%d\n", i);
}

