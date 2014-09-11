#include <wchar.h>
#include <stdio.h>

int
main() {
	wchar_t	x;
#ifdef __GNUC__
	printf("%d %d\n", (int)sizeof x, (int)__alignof x);
#else
	printf("%d %d\n", (int)sizeof x, (int)sizeof x);
#endif
	return 0;
}

