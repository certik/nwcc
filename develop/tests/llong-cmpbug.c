#include <stdio.h>

int
main() {
	printf("%d\n", 8796092497920LLU >= 18302628902545391615LLU);
	if (8796092497920LLU >= 18302628902545391615LLU)
		puts("impossible");
	else
		puts("ok");
}
