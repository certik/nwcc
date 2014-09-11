#include <stdio.h>

/*
 * Test some declaration bugs. Primarily "int (foo)", which
 * was broken at one time - Make sure the fix didn't break
 * other parenthesized things
 */

void
func(int print(const char *, ...), int (val), int (((val2)))) {
	print("%d %d\n", val, val2);
}

int
main() {
	int	(foo) = 123;
	int	((((((((((((((bar)))))))))))))) = 1337; /* GNU coding style :-) */
	func(printf, foo, bar);
}

