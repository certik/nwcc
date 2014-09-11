#include <stdio.h>


int
main() {
	char	buf[128];
	puts(__builtin_memcpy(buf, "hello\0h\0mmm", sizeof "hello"));
	printf("%s\n", buf);
}	
