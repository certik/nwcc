#include "t1.h"
#include <string.h>

void foo(struct bigfoo b);

int
main() {
	struct bigfoo	b;
	struct bigfoo	saved;

	b.x = 123;
	strcpy(b.buf, "hello world");
	b.y = 456;
	b.z = 11114L;
	b.f = 225.3f;
	b.d = 366.526;
	strcpy(b.buf2, "the end");

	saved = b;

	foo(b);

	printf("after call...\n");
	printf("%d,%d\n", b.x, saved.x);
	printf("%s,%s\n", b.buf, saved.buf);
	printf("%d,%d\n", b.y, saved.y);
	printf("%ld,%ld\n", b.z, saved.z);
	printf("%f,%f\n", b.f, saved.f);
	printf("%f,%f\n", b.d, saved.d);
	printf("%s,%s\n", b.buf2, b.buf2);
}

