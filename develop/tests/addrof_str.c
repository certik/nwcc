#include <stdio.h>

int
main() {
	char	(*p)[5];
	p = &"hell"; /* 08/21/08: This was missing, thanks to Harald van Dijk for reporting it! */
	printf("%s\n", *p);
	return 0;
}

