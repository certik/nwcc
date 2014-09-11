#include <stdio.h>

int
main(void) {
	struct foo {
		int	x;
		char	*p;
	} bar, baz, baz2;
	char	buf[128];
	char	*p;
	bar.x = 123;
	bar.p = "hi";
	printf("%d: %s!!\n", bar.x, bar.p);
	baz = bar;
	printf("%d: %s!!\n", baz.x, baz.p);

	p = buf;
	while ((unsigned long)p % sizeof(struct foo)) {
		++p;
	}
	*(struct foo *)p = bar;
	baz2 = *(struct foo *)p;
	printf("%d: %s!!\n", baz2.x, baz2.p);
	

	
#if 0
	} bar = {
		123, "hello"
	}, baz = {
		NULL, 0
	};
#endif

}

