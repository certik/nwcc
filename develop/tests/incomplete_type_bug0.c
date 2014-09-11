#include <stdio.h>

/*
 * 03/03/09: Check that tentative struct declarations of
 * incomplete type are accepted. This is OK if the type
 * is later completed.
 *
 * GNU diffutils needed this
 */
struct foo bar;

void	f();

int
main() {
	/* 
	 * Check that bar is usable for a purpose which does not
	 * require the full type
	 */
	printf("%d\n", &bar != NULL);
	f();

	/*
	 * Check that initializers for POINTERS to incomplete
	 * struct types are still allowed
	 */
	{
		struct bogus *b = (struct bogus *)"hello";
		printf("%s\n", (char *)b);
	}
}

struct foo {
	int	x;
};

struct foo bar = { 567 };

void
f() {
	printf("%d\n", bar.x);
	bar.x = 123;
	printf("%d\n", bar.x);
}

