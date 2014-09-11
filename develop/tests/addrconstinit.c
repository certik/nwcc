#include <stdio.h>

int
main() {
	static struct foo {
		int	x;
		int	*p;
	} bar = {
		128, &bar.x
	};
	printf("%d\n", *bar.p); 
}	

