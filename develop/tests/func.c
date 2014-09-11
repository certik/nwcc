#include <stdio.h>

static void
foo(void) {
	puts(__func__);
}	

static void
bar(void) {
	/* Using both of these identifiers used to fail! */
	puts(__func__);
	puts(__PRETTY_FUNCTION__);
}	

int
main(void) {
	puts(__func__);
	printf("%d\n", (int)sizeof __func__);
	foo();
	return 0;
}

