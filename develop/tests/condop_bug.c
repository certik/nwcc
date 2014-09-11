#include <stdio.h>


int
main() {
	struct foo {
		int	*p;
	} foo_ar[5];
	char	stuff;
	void	*addr;
	char	stuff2;
	int	x = 777;

	foo_ar[3].p = &x;

	addr = &stuff;

	*foo_ar[3].p = (addr == &stuff? 62: ' ');

	printf("%d\n", x);
	addr = &stuff2;
	*foo_ar[3].p = (addr == &stuff? 62: ' ');
	printf("%d\n", x);
}

	
