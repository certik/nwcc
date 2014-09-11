#include <stdio.h>

void
foo(long long val, long long val2) {
	if (val <= val2) {
		puts("f%%%%% bs");
	} else {
		puts("ok");
	}
	printf("%d\n", (val <= val2));
	printf("%d\n", (val >= val2));
	printf("%d\n", (val == val2));
	printf("%d\n", (val != val2));
}	

int
main() {
	long long bs =  0x100000000LL;
	long long bs2 =  0xffffffffLL;
	foo(bs, bs2);
	return 0;
}


