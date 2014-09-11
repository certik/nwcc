#include <wchar.h>
#include <stdio.h>

int
main() {
	printf("%d\n", (int)sizeof L"hello");
	printf("%d\n", (int)sizeof L"hello" / sizeof(wchar_t));
	return 0;
}

