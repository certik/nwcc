#include <stdio.h>

#if 0
char	*foo[];
#endif
char	*foo[5];

static void
func(char *arg[]) {
	printf("%d\n", (int) sizeof arg);
}	

int
main(void) {
	static int	x = sizeof foo;
	printf("%d\n", (int) sizeof foo);
	printf("%d\n", x);
	func(foo);
	return 0;
}

