#include <stdio.h>

void *
mymalloc(size_t gnu) {
	printf("received %lu\n", (unsigned long)gnu);
	return NULL;
}

int
main() {
	char		*p;
	long long	filesize = 163633;

	/*
	 * 08/07/09: VERY basic long long bug! I'm shocked and appalled!
	 * The argument isn't passed correctly. (This bombed in Python
	 * if anyone cares)
	 */
	p = mymalloc((filesize) ? (filesize) : 1);
	return 0;
}

