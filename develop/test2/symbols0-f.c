#include <stdio.h>

/*
 * Test proper global symbol export
 *
 * This test establishes basic linking sanity between multiple
 * nwcc-compiled modules. It was written because the XLATE_IMMEDIATLEY
 * redesign exposed a nasm bug that broke this sanity
 */

int	global_var0;
int	global_var1 = 123;

/* XXX tentative declarations are missing ... */

void
global_func0() {
	printf("global#0: %d, %d\n", global_var0, global_var1);
}

void
global_func1();

void
global_func2() {
	global_var0 = 45226, global_var1 = 36222;
	puts("global#2:");
	global_func1();
	puts("end global#2");
}

extern void
global_func3();

extern int
global_func4() {
	puts("global#4:");
	global_func3();
	return puts("end global#4");
}


void
global_func1() {
	printf("global#1: %d\n", global_var1);
}

void
global_func3() {
	puts("global#3");
}

