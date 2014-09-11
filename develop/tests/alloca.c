#include <stdlib.h>

#ifndef alloca
#define alloca __builtin_alloca
#endif

static int 
foo() {
	char	*p;
	char	*p2;

	if (0) {
		p = alloca(16);
	}
	if (1) {
		p2 = alloca(128);
	}
	strcpy(p2, "hmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmm");
	return puts(p2);
}

static long long
bar() {
	char	*p;

	p = alloca(5);
	strcpy(p, "hm");
	return 12955599955555ll;
}	

static void
check_mem_leak() {
	char	*p = alloca(1000000); /* ~1mb */

	if (p == NULL) {
		abort();
	}	
}	

int
main() {
	char	*p = alloca(128);
	int	i;

	strcpy(p, "hellohellohellohello");
	puts(p);
	p = malloc(128);
	strcpy(p, "hmmmmmmmmm");
	puts(p);
	free(p);
	printf("%d\n", foo());
	printf("%lld\n", bar());
	for (i = 0; i < 10000; ++i) {
		check_mem_leak();
	}	
}

