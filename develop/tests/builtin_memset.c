#include <stdio.h>

int
main() {
	char	buf[] = "     world";
	__builtin_memset(buf, 'h', 4);
	puts(buf);
}

