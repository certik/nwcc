#include <stdio.h>

int
main() {
	static short	buf[128];
	static short	*p = &buf[5];

	int		i;

	for (i = 0; i < 128; ++i) {
		buf[i] = i;
	}	
	printf("%d\n", *p);
}

