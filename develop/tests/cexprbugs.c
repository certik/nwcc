#include <stdio.h>

void
foo() {
	puts("food");
}

void
bar() {
	puts("bar");
}

int
main() {
	char	buf[128];

	printf("%d\n", sizeof(0? foo: bar));
	printf("%d\n", (int)sizeof(0? buf: buf));
	printf("%d\n", (int)sizeof buf);
	printf("%d\n", (int)sizeof (buf));
	printf("%d\n", (int)sizeof ((buf)));
	printf("%d\n", (int)sizeof ((buf+1)));

	(1? foo: bar)();
	(0? foo: bar)();
	return 0;
}

