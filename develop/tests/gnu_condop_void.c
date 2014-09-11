#include <stdio.h>

int
main() {
#if defined __GNUC__ || defined __NWCC__
	printf("%d\n", (int)sizeof(0? (void)0: 0));
	printf("%d\n", (int)sizeof(rand()? (void)0: 0));
#else
	printf("1\n");
	printf("1\n");
#endif
}


