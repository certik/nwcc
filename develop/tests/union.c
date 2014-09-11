#include <stdio.h>

int
main(void) {
	union x {
		int	x;
		int	y;
		char	*p;
	} x, *up = &x;
	static union x	static_u;

	struct {
		char	unused[1337 % 666];
		int	test;
		union x	nested;
	} foo;	
			
	
	x.x = 128;
	printf("%d\n", x.y);
	up->x = 5;
	printf("%d\n", up->x);
	printf("%d\n", x.x);
	static_u.y = 128;
	printf("%d\n", static_u.x);
	x.y = 1234;
	static_u = x;
	printf("%d\n", static_u.x);
	static_u.x = 129;
	*up = static_u;


	printf("%d\n", x.x);
	foo.nested.p = "hello";
	puts(foo.nested.p);
	foo.test = 16;
	return 0;
}

