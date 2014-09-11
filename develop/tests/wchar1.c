#include <wchar.h>
#include <stdio.h>

int
main() {
	wchar_t	x = L'x';
	printf("%d\n", (int)sizeof L'x');
	printf("%d = %c\n", (int)x, (int)x);
	return 0;
}

