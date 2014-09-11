#include <stdio.h>

static void nonsense();

extern void
nonsense() {
	puts("hello world");
}

int
main() {
	nonsense();
}


