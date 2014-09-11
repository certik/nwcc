#include <stdio.h>

int
main() {
	/*
	 * 03/03/09: Items of pointer type which are not backed
	 * by a real address are not properly handled by nwcc in
	 * constant expressions yet.
	 *
	 * (char *)0 + 3
	 *
	 * yields an error because this location in do_eval_const_expr():
	 *
	 *
	 *   * 
	 *   * XXX we need more typechecking
	 *   *
	 *   if (tree->op == TOK_OP_PLUS
	 *      || tree->op == TOK_OP_MINUS) {
	 *      * Must be addr + integer *
	 *
	 * does not handle that case. The expression is bogus because
	 * (char *)0 is not a suitable address constant, but since it
	 * is really backed by an integer, it can be evaluated on all
	 * supported platforms anyway
	 */

	static int foo = (int)((char *)0 + 3);
	printf("%d\n", foo);
	return 0;
}

