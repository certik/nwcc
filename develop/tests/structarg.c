#include <stdio.h>

struct foo {
	int	x;
	char	*y;
	int	z;
};

void
foo(int lol, struct foo p1, int hehe) {
	printf("HERE FOO.X is %d\n", p1.x);
	printf("HERE FOO.Y is %s\n", p1.y);
	printf("HERE FOO.Z is %d\n", p1.z);
	printf("%d %d\n", lol, hehe);
}

int
main(void) {
	struct foo	f;
	f.x = 123;
	f.y = "hello";
	f.z = 456;
	foo(999, f, 777);
	return 0;
}

