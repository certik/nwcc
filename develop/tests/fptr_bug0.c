#include <stdio.h>

/*
 * 05/16/09: Because we didn't appropriately reset the
 * lvalue flag for function calls, the expression
 * foo()() (where foo() returns a function pointer)
 * used the argument count information of the foo()
 * declaration for both calls. So there was an error
 * if the second call had an argument count mismatch
 */
void
bar(void) {
	puts("in bar");
}

void
(*foo(int x))(void) {
	printf("in foo (arg %d)\n", x);
	return bar;
}


int
main() {
	foo(3)();
}

