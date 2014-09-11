#include <stddef.h>

struct str {
	int	x;
	char	y;
	long z;
	double lol;
	char hheh;
};

int
main() {
	printf("%d\n", offsetof(struct str, x));
	printf("%d\n", offsetof(struct str, y));
	printf("%d\n", offsetof(struct str, z));
	printf("%d\n", offsetof(struct str, lol));
	printf("%d\n", offsetof(struct str, hheh));
}

