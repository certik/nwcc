#include <wchar.h>
#include <stdio.h>


int
main() {
	static wchar_t	*str = L"hello world";
	wprintf(L"%ls\n", str);

	return 0;
}

