#include <stdio.h>

void
foo(long long val, long long val2) {
	if (val >= val2) {
		puts("f%%%%% bs");
	}
	printf("%d\n", ((val >= val2)));
	printf("%d\n", ((val <= val2)));
	printf("%d\n", ((val == val2)));
	printf("%d\n", ((val != val2)));
}	

int
main() {
	long long bs = -1LL;
	foo(bs, 0);
	return 0;
}


