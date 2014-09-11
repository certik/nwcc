#include <stdio.h>

int
main() {
	char (*p)[128] = malloc(sizeof(char[128]));
	puts(strcpy(*p, "lolzzz"));
}
