#include <stdio.h>

int
main() {
	struct foo {
		unsigned	f:31;
		unsigned	g:1;
		char		x; /* align me */
	};
	struct foo	fa[] = {
		{ 0xefefef , 1 , 10 }
	};
	int	i;


	printf("%d\n", sizeof (struct foo));

	printf("%u, %u, %u\n", fa[0].f, fa[0].g, fa[0].x);

	return 0;
}

