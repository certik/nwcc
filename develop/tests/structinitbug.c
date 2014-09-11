#include <stdio.h>

struct foo {
	int	x;
	char	*p;
} baz() {
	struct foo	f;
	f.x = 123;
	f.p = "hello";
	return f;
}

union lol {
	char	buf[128];
} un() {
	union lol	l;
	strcpy(l.buf, "HEHEHEHEHEHEHEHEHHEHHEHEHEHEHEHE LOLOLOL");
	return l;
}

int
main() {
	struct foo	bar = baz();
	union lol	l = un();

	printf("%d: %s\n", bar.x, bar.p);
	puts(l.buf);
	return 0;
}	

