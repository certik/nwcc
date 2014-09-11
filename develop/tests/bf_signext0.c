#include <stdio.h>
#include <string.h>

int
main() {
	struct foo {
		int	x:1;
		int	y:2;
		int	z:3;
		int	a:4;
		int	b:5;
		int	c:6;
		int	d:14;
		int	e:4;
		int	f:15;
	} f;

	f.x = -1;
	f.y = -1;
	f.z = 0x7;
	f.a = -2;
	f.b = 0x11;
	f.c = 0x3f;
	f.d = -465;
	f.e = 13;
	f.f = -333;
	printf("%d %d %d %d %d %d %d %d %d\n",
		f.x, f.y, f.z, f.a, f.b, f.c, f.d, f.e, f.f);

        return 0;
}

