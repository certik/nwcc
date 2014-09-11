#include <stdio.h>
#include <string.h>

int
main(void) {
	struct foo {
		int	x;
		char	*p;
	} f[2], b;

	char	buf[128];

	strcpy(buf, "hello");

	f[0].x = 123;
	f[0].p = buf;
	printf("%d %s\n", f[0].x, f[0].p);
	b = f[0];
	printf("%d %s\n", b.x, b.p);
}	
		

