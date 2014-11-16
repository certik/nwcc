#include <stdio.h>

typedef enum gnu foo;

enum gnu {
	zorg = 123
};

enum trole;

enum trole {
	trole,
	gnew
};


int
main() {
	foo f = zorg;
	printf("%d\n", (int)f);
	printf("%d\n", gnew);
}

