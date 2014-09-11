#include <stdio.h>

int
main() {
	/*
	 * Test that usual arithmetic conversion is performed properly, with
	 * partially and fully constant expressions.
	 *
	 * There were no bugs here, but it's good to have a test for it
	 */
	long long	LL;
	static int	size = sizeof (1? 0: 0LL);
	static int	size2 = sizeof (1? 0LL: 0);
	auto int	size3 = sizeof (1? 0: LL);
	auto int	size4 = sizeof (1? LL: 0);

	printf("%d\n", size);
	printf("%d\n", size2);
	printf("%d\n", size3);
	printf("%d\n", size4);
	return 0;
}

