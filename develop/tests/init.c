#include <stdio.h>

int
main(void) {
	int	i;
	int	j;

	struct foo;
	struct foo;
	struct foo;
	struct foo {
		short	bleh;
		float	well;
		char	buf[5];
		int	hm;
		int	twodim[2][1];
	} f = {
		4, 0.6f, {1, 2, 3, 4, 5}, 1337,
		1, 17	
	};		
	static struct foo	hm = {1};

	struct foo;
	printf("%d\n", f.bleh);
	printf("%f\n", f.well);
	for (i = 0; i < 5; ++i) {
		printf("%d\n", f.buf[i]);
	}
	printf("%d\n", f.hm);
	printf("%d %d\n", f.twodim[0][0], f.twodim[1][0]);
	printf("%d %d\n", hm.bleh, hm.hm);
}

