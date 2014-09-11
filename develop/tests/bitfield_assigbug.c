#include <stdio.h>

int
main() {
	int j = 1081;
	struct {
		signed int m:11;
	} s;
	if ((s.m = j) == j) {
		printf("hmm %d = %d\n", s.m, j);
	}
	printf("%d\n", s.m);
	return 0;
}

