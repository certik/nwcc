#include <stdio.h>


#define stupid_offsetof_bullshit(type, member) \
	((size_t)&(((type *)0)->member))

int
main() {
	struct barf {
		int	x;
		char	*p;
		struct nonsense {
			int x;
		} y;
		char	bogus[128][25];
		int	z;
	};
	static int	bla = stupid_offsetof_bullshit(struct barf, y);
	static int	bla2 = stupid_offsetof_bullshit(struct barf,
			bogus[3][2]);
	printf("%d\n", bla);
	printf("%d\n", bla2);
}

