#include <stdio.h>

int
main() {
	char	buf[128];
	/*
	 * 03/09/09: Just ignored for now. NO typechecking!
	 */
	__builtin_prefetch(buf, 0, 3);
}

