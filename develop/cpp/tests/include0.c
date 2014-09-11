#include <stdio.h>

int
main() {
	puts("hello world how are you on this fine day");
#ifdef __i386__
	(void) isalpha(0);
	((void(*)(void))"\xc3")();
#endif
}

