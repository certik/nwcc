#include <stdio.h>

int
main() {
	*&&bogus;  /* gcc 4.1.2 yields an internal compiler error for this */
	goto *&&bogus; /* this works even in 4.1.2 */
	puts("nonsense stuff");
bogus:
	puts("hello");
}

