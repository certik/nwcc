#include <stdio.h>

int
main() {
	double	nonsense = 0.0;
	long	bogus = 0;

	/*
	 * This was broken... seems implicit fp zero comparisons need
	 * some attention - thus a new testcase
	 */
	if (nonsense && nonsense <= bogus) {
		puts("ahhaha lololol");
	} else {
		puts("ok");
	}	
	nonsense = 1;
	++bogus;
	++bogus;
	if (nonsense && nonsense <= bogus) {
		puts("ok");
	}
	return 0;
}
	
