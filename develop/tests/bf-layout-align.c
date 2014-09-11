#include <stdio.h>


int
main() {
	struct foo {
		int	bar:32;
	};
	struct foo2 {
		int	bar:1;
	};
	struct foo3 {
		char	x;
		int	bar:1;
	};
	struct foo4 {
		char			x;
		unsigned long long	bar:48;
	};

	printf("%d\n", __alignof(struct foo));
	printf("%d\n", __alignof(struct foo2));
	printf("%d\n", __alignof(struct foo3));
	printf("%d\n", __alignof(struct foo4));
	return 0;
}
