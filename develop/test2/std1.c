#include <stdio.h>

int fileno(char *txt) { return puts(txt); }

int
main() {
	printf("%d\n", fileno("hello world"));
}

