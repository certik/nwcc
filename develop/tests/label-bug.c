#include <stdio.h>
#include <stdlib.h>

int
main() {
	int	i = 5;

	goto foo;
	if (0) foo: bar: baz: puts("hello");
	else {
		do
			hello: puts("good");
		while (--i > 3);
	}
	
	if (i <= 3) {
		goto out;
	} else {	
		goto hello;
	}
	for (;;) puts("NOTREACHED");
	for (;;) out: puts("goodbye"), exit(0);
}
