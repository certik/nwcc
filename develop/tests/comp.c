#include <stdio.h>

int
main(void) {
	char	*p;
	char	c;
	typedef int	x;
	/* x x;  gcc bug? */

	p = &c;
	c = 123;
	*p &= 0xf;
	printf("%d\n", c);

	while (c /= 2) {
		puts("he he");
	}
	return 0;
}	

