#include <stdio.h>

int
main() {
	int		i;
	long		l;
	long long	ll;

	/* Make sure that << and >> do not convert */
	if (sizeof(123 << 1L) != sizeof(int)) {
		puts("bugged");
	} else {
		puts("ok");
	}
	if (sizeof(i << l) != sizeof(int)) {
		puts("bugged");
	} else {
		puts("ok");
	}


	/* Another time for long long (on systems where int equ long) */
	if (sizeof(123 << 1LL) != sizeof(int)) {
		puts("bugged");
	} else {
		puts("ok");
	}
	if (sizeof(i << ll) != sizeof(int)) {
		puts("bugged");
	} else {
		puts("ok");
	}


	/* Make sure that the shift target is promoted */
	printf("%d, %d\n", (char)3 << 4, (char)64 >> 2);
	printf("%d, %d\n", (int)sizeof((char)3 << 4), (int)sizeof((char)64 >> 2));

	/* Make sure that comma operator yields neither promotion nor UAC */
	printf("%d\n", (int)sizeof((char)0, (char)1));
	return 0;
}

