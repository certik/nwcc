#include <stdio.h>

#define ret(x) do { \
	printf("foofoofoo %d\n", __LINE__); \
	return (x); \
} while (0)

int
main(void) {
	if (printf("HA HA HA")) ret(5);
	if (1) ret(0);
}	

		
