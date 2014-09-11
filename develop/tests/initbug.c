#include <stdio.h>

int
main() {
	struct foo {
		int	x;
		char	y;
		char	z[3][4];
		int	a;
	} f = {
		123, 45,
		{ "lol", "olo" },
		777
	};
	static char	buf[] = { 'x', 'y', 'z', 'a', 'b', '\0' };
	static char	*p = buf + 5 - 2;

	puts(p);
	printf("%d, %d, %s, %s,  %d,%d,%d,%d, %d\n",
		f.x, f.y, f.z[0], f.z[1],
		f.z[2][0], f.z[2][1], f.z[2][2], f.z[2][3],
		f.a);
	return 0;
}	
		

