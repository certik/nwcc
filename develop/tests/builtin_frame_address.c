#include <stdio.h>


/*
 * builtin frame address. This test is a bit silly. It only tests
 * if the current stack frame's frame pointer is available, and if
 * the local variable ``p'' is declared is SOME proximity to that
 * address.
 */
int
main() {
	char	*p = (char *)&p;
	char	*fp = __builtin_frame_address(0);
	int	ok = 0;

	if (fp >= p && fp - 128 < p) {
		ok = 1;
	}
	if (fp <= p && fp + 128 > p) {
		ok = 1;
	}
	if (ok) {
		puts("OK");
	} else {
		puts("BUG");
	}	
}

