#include <stdio.h>

int
main() {
	printf("%d\n", sizeof((char)0 | (char)1));
	printf("%d\n", sizeof((char)0 | (short)1));
	printf("%d\n", sizeof((int)0 | (char)1));
	printf("%d\n", sizeof((long)0 | (char)1));
	printf("%d\n", sizeof((unsigned short)0 | (int)1));
	printf("%d\n", sizeof((long long)0 | (int)(double)1));
	printf("%d\n", sizeof((long long)0 | (short)(long double)1));
	printf("%d\n", sizeof((int)(float)0 | (unsigned char)1));
	return 0;
}

