#include <stdio.h>

int
main() {
	/* Check basic skipping of unnamed members */
	struct bogus {
		int	:16;
		int	x:17;
	} b = {
		123
	};

	/* Same but with unnamed initializer being last member */
	struct bogus2 {
		int	:16;
		int	x:17;
		int	:16;
	} b2 = {
		123
	};


	printf("%d\n", (int)sizeof(struct bogus));
	printf("%d\n", b.x);

	printf("%d\n", (int)sizeof(struct bogus2));
	printf("%d\n", b2.x);
	return 0;
}

