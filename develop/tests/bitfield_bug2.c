#include <stdio.h>

int
main() {
	struct foo {
		int	x:4; 
		int	y:4;
		int	z:9;
		int	dummy:2;
	};

	struct foo	f;
	struct foo	*fp;

	f.x = 1;
	f.y = 0;
	f.z = 44;
	f.dummy = 2;

	fp = &f;


	printf("%d\n", fp->y && fp->y);
	printf("%d\n", (fp->y && fp->y));

	printf("%d\n", (f.y += f.z));
}


