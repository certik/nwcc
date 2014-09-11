#include <stdio.h>


int
main() {
	long long	dest = 123LL;
	long long	src = -53LL;

	printf("%lld\n", dest / src);
	printf("%lld\n", dest % src);
	dest = -53LL;
	src = 11;
	printf("%lld\n", dest / src);
	printf("%lld\n", dest % src);
	src = -11;
	printf("%lld\n", dest / src);
	printf("%lld\n", dest % src);
}

