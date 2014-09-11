#include <stdio.h>


struct foo {
	int	x;
	double	y;
} __attribute__((aligned(16))) f; 


struct bar {
	int		x;
	long double	y;
} __attribute__((packed));

struct baz {
	int	x __attribute__((aligned(16)));
};	


int
main() {
	struct bar	b;
	printf("%d\n", (int)__alignof(struct foo));
	printf("%d,%d\n", (int)__alignof(struct bar), (int)sizeof b);
	printf("%d\n", (int)__alignof(struct baz));
}

