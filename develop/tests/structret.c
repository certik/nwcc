#include <stdio.h>

struct foo {
	int	x;
	char	*p;
};

struct foo
func(void) {
	struct foo	ret;
	ret.x = 123;
	ret.p = "hello";
	return ret;
}

union bar {
	int	hehe;
	char	hm[2];
};	

union bar
func2(int x) {
	union bar	ret;
	ret.hehe = x;
	return ret;
}	

int
main(void) {
	struct foo	f;
	union bar	b;
	f = func();
	printf("results of func: %d,%s\n", f.x, f.p);
	b = func2(456);
	printf("results of func2: %d %d\n", b.hm[0], b.hm[1]); 
	return 0;
}

