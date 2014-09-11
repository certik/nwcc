#include <stdio.h>

struct foo {
	int	dataRead;
	int	bufpos;
};

int
main() {
	unsigned long long blocksize;
	unsigned long long size;
	struct foo	f;
	struct foo	*thefile = &f;
	int		x = 0;

	size = 34LL;
	f.dataRead = 123;
	f.bufpos = 22;


	blocksize = 123;
	sync();
	blocksize = x? thefile->dataRead : size;

	printf("%lld\n", blocksize);
}


