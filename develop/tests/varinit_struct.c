#include <stdio.h>

struct foo {
	int	x;
	int	y;
	struct {  /* nested struct to check that the varinit initializer is
		   * correctly computed
		   */
		char	first;
		int	second;
	} hm;
	int	z;
};

int
main() {
	int		x = 123;
	int		y = x * 12;
	struct foo	f = { 567, x, { 14 }, y }; 
	printf("%d, %d, %d, %d, %d\n", f.x, f.y, f.hm.first, f.hm.second, f.z);
}

