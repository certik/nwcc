#include <stdio.h>

int
main() {
	long long	ll_lower;
	long long	ll_upper;
	ll_lower = 0xffffffffu;
	printf("%lld\n", ll_lower);
	ll_upper = 0xffffffffffffffffLLU;
	printf("%lld\n", ll_upper);
}	

