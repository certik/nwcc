#include <stdio.h>

int
main() {
	long long	ll_full =  0xffffffffffffffff;
	long long	ll_upper = 0xffffffff00000000;
	long long	ll_lower = 0xffffffff;
	long long	one = 0x1;

	if (ll_full < 1) {
		puts("bug");
	}	
	if (ll_upper <= 0xffffffff) {
		puts("bug2");
	}
	if (0xffffffffff > ll_upper) {
		puts("bug3");
	}
	if (ll_upper == ll_lower) {
		puts("bug4");
	}
	if (ll_lower == ll_full) {
		puts("bug5");
	}
	if (ll_lower >= ll_full) {
		puts("bug6");
	}
	if (one > 0xffffffff) {
		puts("bug7 [gxxemul]");
	}	
}	
