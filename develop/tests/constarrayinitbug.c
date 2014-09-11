#include <stdio.h>
#include <string.h>

/*
 * 03/01/09: Like
 *
 *    static char buf[128];
 *    static char *p = buf;
 *
 * ... the same thing should be allowed with 2d arrays, where
 * the address is unambiguously computable at link time 
 */
int
main() {
	static char	buf[2][128];
	static char	buf2[2][2][128];
	static struct foo {
		char	*p1;
		char	*p2;
	} f = {
		buf[0], buf[1]
	};
	static struct bar {
		char	*p1;
		char	*p2;
	} b = {
		buf2[0][1], buf2[1][0]
	};

	strcpy(f.p1, "hello");
	strcpy(f.p2, "world");
	printf("%s, %s\n", buf[0], buf[1]);

	strcpy(b.p1, "bogus");
	strcpy(b.p2, "stuff");
	printf("%s %s\n", buf2[0][1], buf2[1][0]);
}

