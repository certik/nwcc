#include <stdio.h>


typedef char	buf[20];


void foo(buf b) {}

int
main() {
	buf	b;
	printf("%d\n", (int)sizeof b);
	return 0;
}	



