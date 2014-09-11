#include <stdio.h>

int
main(void) {
	unsigned long long	foo = 123ll;
	unsigned long long	bar = 1841;
	signed long long	foos = -128;
	signed long long	bars = 15;
	long long		x = 12252224;
	long long		y = 11225;

	printf("foo=%llu,bar=%llu\n", foo, bar);
	printf("foo + bar = %llu\n", foo + bar);
	printf("foo - bar = %llu\n", foo - bar);
	printf("foo >> 1 = %llu\n", foo >> 1);
	printf("foo << 3 = %llu\n", foo << 3);
	/*printf("bar / foo = %llu\n", bar / foo);*/
	printf("foos = %lld\n", foos);
	printf("bars = %lld\n", bars);
	printf("x / y = %lld, x % y = %lld\n", x / y, x % y);
	printf("x * y = %lld\n", x * y);
	/*printf("foos / bars = %lld\n", foos / bars);*/
	return 0;
}	
