#include <stdio.h>
#include <string.h>


static struct foo {
	char	buf[128];
	char	c;
} f;

/*
 * Constant expression initializer bug for array struct members -
 * reported by Urs Janßen
 */
char	*p = f.buf;

int
main() {
	strcpy(p, "hello");
	puts(f.buf);
}
