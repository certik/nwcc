#include <stdio.h>

struct foo {
	int	x;
	char	*y;
	int	z;
} maketemp(int x, char *y, int z) {
	struct foo	ret;
	ret.x = x;
	ret.y = y;
	ret.z = z;
	return ret;
}


struct foo
doit(struct foo f, int print) {
	f.x *= 2;
	f.z /= 3;
	f.y += 4;
	if (print) {
		printf("%d, %s, %d\n", f.x, f.y, f.z);
	}
	return f;
}

int
main() {
	printf("%s\n", maketemp(123, "helhelele", 32).y);
	doit(maketemp(123, "hello world", 456), 1);
	printf("%d\n", doit( maketemp(123, "hello world", 456), 0 ).x);
	printf("%s\n", doit( maketemp(123, "hello world", 456), 0 ).y);
	printf("%d\n", doit( maketemp(123, "hello world", 456), 0 ).z);
}

