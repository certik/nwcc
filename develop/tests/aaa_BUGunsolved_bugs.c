__extension__ int x_ __attribute__((aligned(16)));


#include <stdio.h>

int
x(int arg0, int arg1) {
	printf("%d, %d\n", arg0, arg1);
	return arg0 + arg1;
}

/*
 * The function definition below yields a syntax error. Sadly it is used
 * by Pine.
 *
 * The reason is that nwcc simplistically assumes that a K&R parameter
 * list is immediately followed by the parameter declarations. Thus it
 * does not handle the case below, where there is some more declarator
 * stuff around the parameter list. This requires some nontrivial changes
 * and is probably not VERY important to support, so it will be done later
 */
int
(*f(foo, bar))()
	int	foo, bar;
{
	return x;
}


int
main() {
	typedef int	foo;

foo: ;   /* should not clash with "foo" typedef */
	printf("%d\n", f()(123, 456));
}

