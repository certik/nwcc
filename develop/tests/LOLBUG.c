#include <stdio.h>

int
main() {
	unsigned long long	ll = 0x000000001fc00000;

	if (ll > 0xffffffff) {
		puts("BUG");
	}	
}

