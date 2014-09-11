#include <limits.h>

int
main() {
	printf("%u\n", (1 << CHAR_BIT - 1) | (256 >> 2));
	printf("%llx\n", (unsigned long long)((unsigned char)0x80 << 24));
	return 0;
}

