#include <stdio.h>

int
main() {
	/*
	 * 03/09/09: OK, let's give in and be more permissive in pointer to and
	 * from integer conversions (only warn instead of giving an error - like
	 * gcc), while also testing that the assignments do the expected thing
	 */
	int	x = (void *)123;
	void	*p = 456;
	printf("%d\n", x);
	printf("%d\n", (int)p);
}

