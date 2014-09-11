#include <stdio.h>

struct foo {
	int	x;
	char	*y;
	int	z;
};

void
print(struct foo f, int dummy, int dummy2) {
	printf("%d, %s, %d   [%d,%d]\n", f.x, f.y, f.z, dummy, dummy2);
}
	

int
main() {
	struct foo	f = { 123, "hello", 456 };
	struct foo	f2 = { 456, "world", 123 };
	struct foo	*fp1 = &f;;
	struct foo	*fp2 = NULL;

	sync();
	print(fp2? *fp2: f2, 128, 444);
	sync();
	print(fp1? *fp1: f2, 18, 44);
	printf("%s\n", (fp1? *fp1: f2).y);
}

